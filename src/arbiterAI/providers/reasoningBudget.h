#ifndef _ARBITERAI_PROVIDERS_REASONINGBUDGET_H_
#define _ARBITERAI_PROVIDERS_REASONINGBUDGET_H_

#include <cstdint>
#include <optional>
#include <string>

namespace arbiterAI
{

/// Token ids of a model's <think> and </think> tags; -1 when the vocabulary
/// has no single token for the tag.
struct ThinkTokens {
    int32_t open=-1;
    int32_t close=-1;

    bool valid() const { return open>=0&&close>=0; }
};

/// Applies runtime_options.reasoning_budget to one generation on a model that
/// uses <think> blocks.  Pure bookkeeping: the caller feeds it the prompt and
/// every generated token and does the prefill/injection itself.
///
/// budget unset, or a model without think tokens: a no-op.  budget 0: the
/// prompt gets an empty thinking block (prefillText).  budget N: once N tokens
/// have been generated inside a thinking block, observe() asks for the close
/// (closeText) to be injected.
class ReasoningBudget {
public:
    ReasoningBudget(std::optional<int> budget, const ThinkTokens &tokens);

    static const std::string &openTag();
    static const std::string &closeTag();
    static const std::string &prefillText();
    static const std::string &closeText();

    bool enabled() const;
    bool wantsPrefill() const;

    /// The prompt as tokenized (prefill included), so a prompt ending inside
    /// a thinking block starts the generation in it.
    void observePrompt(const std::string &prompt);

    /// One generated token and its text piece; true when the close must be
    /// injected before the next token is sampled.
    bool observe(int32_t token, const std::string &piece);

    /// The caller injected closeText().
    void injected();

    bool inThinking() const { return m_inThinking; }
    int thinkingTokens() const { return m_thinkingTokens; }
    int injections() const { return m_injections; }

private:
    void scanText(const std::string &text);

    std::optional<int> m_budget;
    ThinkTokens m_tokens;
    bool m_inThinking=false;
    int m_thinkingTokens=0;
    int m_injections=0;
    std::string m_tail;
};

}//namespace arbiterAI

#endif//_ARBITERAI_PROVIDERS_REASONINGBUDGET_H_
