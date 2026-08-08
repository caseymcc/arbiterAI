#include "arbiterAI/chatFormat.h"

#include <chat.h>
#include <llama.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

namespace arbiterAI
{

struct ChatPrompt::Impl {
    common_chat_params params;
    /// params.parser is the generated parser *source*; parsing needs it loaded
    /// into an arena.  Built once here rather than per streaming chunk.
    common_peg_arena arena;
};

struct ChatFormat::Impl {
    common_chat_templates_ptr templates;
};

namespace
{

/// Translate arbiterAI messages into common_chat's representation.
/// Image parts are deliberately not passed through: the multimodal path builds
/// its own prompt with media markers, and common_chat has no notion of our
/// ContentPart images.
std::vector<common_chat_msg> toCommonMessages(const std::vector<Message> &messages)
{
    std::vector<common_chat_msg> result;
    result.reserve(messages.size());

    for(const Message &message:messages)
    {
        common_chat_msg converted;
        converted.role=message.role;
        converted.content=message.content;

        if(message.toolCallId.has_value())
        {
            converted.tool_call_id=message.toolCallId.value();
        }
        if(message.name.has_value())
        {
            converted.tool_name=message.name.value();
        }
        if(message.toolCalls.has_value())
        {
            for(const ToolCall &call:message.toolCalls.value())
            {
                common_chat_tool_call convertedCall;
                convertedCall.id=call.id;
                convertedCall.name=call.name;
                convertedCall.arguments=call.arguments.is_string()
                    ?call.arguments.get<std::string>()
                    :call.arguments.dump();
                converted.tool_calls.push_back(std::move(convertedCall));
            }
        }
        result.push_back(std::move(converted));
    }
    return result;
}

std::vector<common_chat_tool> toCommonTools(const std::vector<ToolDefinition> &tools)
{
    std::vector<common_chat_tool> result;
    result.reserve(tools.size());

    for(const ToolDefinition &tool:tools)
    {
        common_chat_tool converted;
        converted.name=tool.name;
        converted.description=tool.description;
        converted.parameters=tool.parametersSchema.is_null()
            ?std::string("{}")
            :tool.parametersSchema.dump();
        result.push_back(std::move(converted));
    }
    return result;
}

} // namespace

// ── ChatPrompt ────────────────────────────────────────────────

ChatPrompt::ChatPrompt():
    m_impl(std::make_unique<Impl>())
{
}

ChatPrompt::~ChatPrompt()=default;

const std::string &ChatPrompt::text() const
{
    return m_impl->params.prompt;
}

bool ChatPrompt::supportsThinking() const
{
    return m_impl->params.supports_thinking;
}

std::string ChatPrompt::formatName() const
{
    return common_chat_format_name(m_impl->params.format);
}

const std::vector<std::string> &ChatPrompt::additionalStops() const
{
    return m_impl->params.additional_stops;
}

ChatParseResult ChatPrompt::parse(const std::string &output, bool isPartial) const
{
    ChatParseResult result;

    common_chat_msg parsed;
    try
    {
        common_chat_parser_params parserParams(m_impl->params);
        // DEEPSEEK reports reasoning through reasoning_content, including in
        // streaming deltas, and is llama-server's own default.
        parserParams.reasoning_format=COMMON_REASONING_FORMAT_DEEPSEEK;
        parserParams.parser=m_impl->arena;

        parsed=common_chat_parse(output, isPartial, parserParams);
    }
    catch(const std::exception &e)
    {
        // Better to hand back an unsplit answer than to lose the response.
        spdlog::warn("[chat] failed to parse model output ({}); returning it unsplit", e.what());
        result.content=output;
        return result;
    }

    result.content=parsed.content;
    result.reasoningContent=parsed.reasoning_content;

    for(const common_chat_tool_call &call:parsed.tool_calls)
    {
        ToolCall converted;
        converted.id=call.id;
        converted.name=call.name;
        try
        {
            converted.arguments=nlohmann::json::parse(call.arguments);
        }
        catch(const std::exception &)
        {
            converted.arguments=call.arguments;
        }
        result.toolCalls.push_back(std::move(converted));
    }

    return result;
}

// ── ChatFormat ────────────────────────────────────────────────

ChatFormat::ChatFormat():
    m_impl(std::make_unique<Impl>())
{
}

ChatFormat::~ChatFormat()=default;

std::unique_ptr<ChatFormat> ChatFormat::create(llama_model *model)
{
    if(!model)
    {
        return nullptr;
    }

    std::unique_ptr<ChatFormat> format(new ChatFormat());

    try
    {
        format->m_impl->templates=common_chat_templates_init(model, "");
    }
    catch(const std::exception &e)
    {
        spdlog::warn("[chat] no usable chat template ({}); using the built-in prompt path", e.what());
        return nullptr;
    }

    if(!format->m_impl->templates)
    {
        return nullptr;
    }

    return format;
}

std::string ChatFormat::source() const
{
    return common_chat_templates_source(m_impl->templates.get());
}

std::shared_ptr<ChatPrompt> ChatFormat::apply(const std::vector<Message> &messages,
    const std::vector<ToolDefinition> &tools) const
{
    common_chat_templates_inputs inputs;
    inputs.messages=toCommonMessages(messages);
    inputs.tools=toCommonTools(tools);
    inputs.add_generation_prompt=true;
    inputs.use_jinja=true;
    inputs.reasoning_format=COMMON_REASONING_FORMAT_DEEPSEEK;

    std::shared_ptr<ChatPrompt> prompt(new ChatPrompt());

    try
    {
        // Serialised: common_chat_templates_apply renders through a jinja
        // environment we don't own and don't have re-entrancy guarantees for.
        // Template rendering is microseconds next to inference, so the
        // contention cost is irrelevant.
        std::lock_guard<std::mutex> lock(m_applyMutex);
        prompt->m_impl->params=common_chat_templates_apply(m_impl->templates.get(), inputs);

        if(!prompt->m_impl->params.parser.empty())
        {
            // An empty arena makes common_chat_peg_parse fall back to treating
            // the whole response as content — the right degradation for a
            // template that declares no structure.
            prompt->m_impl->arena.load(prompt->m_impl->params.parser);
        }
    }
    catch(const std::exception &e)
    {
        spdlog::warn("[chat] failed to apply chat template: {}", e.what());
        return nullptr;
    }

    return prompt;
}

} // namespace arbiterAI
