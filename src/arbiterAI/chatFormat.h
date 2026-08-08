#ifndef _ARBITERAI_CHATFORMAT_H_
#define _ARBITERAI_CHATFORMAT_H_

#include "arbiterAI/arbiterAI.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration for llama.cpp types
struct llama_model;

namespace arbiterAI
{

/// Result of splitting a model's raw output into its parts.
struct ChatParseResult {
    std::string content;            ///< The answer the client should see
    std::string reasoningContent;   ///< Chain-of-thought, when the model emits one
    std::vector<ToolCall> toolCalls;
};

/**
 * @class ChatPrompt
 * @brief A rendered prompt plus the parser its template implies.
 *
 * Response parsing is driven by the same template application that produced the
 * prompt, so the two travel together: a request holds its ChatPrompt from
 * tokenization through to parsing the reply.
 */
class ChatPrompt {
public:
    ~ChatPrompt();

    ChatPrompt(const ChatPrompt &)=delete;
    ChatPrompt &operator=(const ChatPrompt &)=delete;

    /// The rendered prompt to tokenize.
    const std::string &text() const;

    /// Split a (possibly partial) model output into content, reasoning and
    /// tool calls.  `isPartial` must be true mid-stream so an unterminated
    /// block isn't treated as malformed.
    ChatParseResult parse(const std::string &output, bool isPartial) const;

    /// True when the template indicates this model produces reasoning.
    bool supportsThinking() const;

    /// Format name, for logging.
    std::string formatName() const;

    /// Stop strings the template declares beyond the vocab's EOG tokens.
    const std::vector<std::string> &additionalStops() const;

    /// Whether the prompt actually fed to the model included this template's
    /// generation prefix.  Templates may pre-fill the start of the assistant
    /// turn (Qwen3-VL opens `<think>` for the model to continue inside), and
    /// parsing prepends that prefix to the output to reconstruct the full turn.
    /// A caller that built its own prompt — the multimodal path renders media
    /// markers itself — must clear this, or the model's own opening tag is
    /// parsed as reasoning *text* rather than as the tag.
    void setGenerationPrefixApplied(bool applied);

    /// The pre-filled start of the assistant turn this template emits (Qwen3-VL
    /// opens `<think>` for the model to continue inside).  A caller building its
    /// own prompt must append this, or the model re-emits the opening tag and
    /// the parser — which reconstructs the turn from this prefix — sees it twice.
    const std::string &generationPrefix() const;

private:
    friend class ChatFormat;
    ChatPrompt();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @class ChatFormat
 * @brief Chat-template-driven prompt building and response parsing.
 *
 * Models wrap reasoning and tool calls in their own markup — harmony channels
 * for gpt-oss, `<think>` tags for Qwen3 Thinking and the DeepSeek-R1 distills,
 * others elsewhere.  Rather than maintain a parser per family, the format is
 * derived from the model's own chat template (GGUF `tokenizer.chat_template`)
 * through llama.cpp's `common_chat`, so a new reasoning model works without a
 * code change here.
 *
 * llama.cpp's `common` is an internal utility library with no API stability
 * guarantee, so it is confined to chatFormat.cpp: none of its types appear in
 * this header, and nothing consuming libarbiterai needs its headers.
 *
 * One ChatFormat per loaded model, shared across requests.  apply() is
 * serialised internally, so callers on different threads are safe.
 */
class ChatFormat {
public:
    /// Build a ChatFormat for a loaded model.  Returns nullptr when the model
    /// ships no usable chat template, so callers fall back to the built-in path.
    static std::unique_ptr<ChatFormat> create(llama_model *model);

    ~ChatFormat();

    ChatFormat(const ChatFormat &)=delete;
    ChatFormat &operator=(const ChatFormat &)=delete;

    /// Render messages into a prompt carrying its own parser.
    /// Returns nullptr if the template could not be applied.
    std::shared_ptr<ChatPrompt> apply(const std::vector<Message> &messages,
        const std::vector<ToolDefinition> &tools) const;

    /// The template's origin (built-in name or "custom"), for logging.
    std::string source() const;

private:
    ChatFormat();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    mutable std::mutex m_applyMutex;
};

} // namespace arbiterAI

#endif//_ARBITERAI_CHATFORMAT_H_
