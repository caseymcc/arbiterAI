#ifndef _ARBITERAI_THINKTAGS_H_
#define _ARBITERAI_THINKTAGS_H_

#include <string>

namespace arbiterAI
{

/// Reasoning models outside the harmony family (Qwen3 Thinking, DeepSeek-R1
/// distills, …) wrap their chain-of-thought in <think>…</think> inside the
/// normal content stream.  Clients that don't strip it show the reasoning as
/// the answer, so it is split into reasoning_content the same way harmony's
/// analysis channel is.

struct ThinkTagParseResult {
    std::string content;
    std::string reasoningContent;
};

/// Split a completed response into content and reasoning.
/// An unterminated <think> (generation stopped mid-thought) puts the remainder
/// in reasoning, leaving content empty rather than leaking a partial thought as
/// the answer.
ThinkTagParseResult parseThinkTags(const std::string &text);

/// Streaming counterpart of parseThinkTags(): routes each piece of the token
/// stream to either content or reasoning as it arrives.  Handles tags split
/// across chunk boundaries.
class ThinkTagStreamParser {
public:
    /// Feed a token chunk, appending to contentOut / reasoningOut.
    void feed(const std::string &chunk, std::string &contentOut, std::string &reasoningOut);

    /// Emit anything still buffered once the stream ends.
    void flush(std::string &contentOut, std::string &reasoningOut);

    /// Reasoning accumulated so far.
    const std::string &reasoning() const { return m_reasoning; }

    /// True while the parser sits inside a <think> block.
    bool inThink() const { return m_inThink; }

private:
    std::string m_buffer;
    std::string m_reasoning;
    bool m_inThink=false;
};

} // namespace arbiterAI

#endif//_ARBITERAI_THINKTAGS_H_
