#include "arbiterAI/providers/reasoningBudget.h"

namespace arbiterAI
{

ReasoningBudget::ReasoningBudget(std::optional<int> budget, const ThinkTokens &tokens):
    m_budget(budget),
    m_tokens(tokens)
{
}

const std::string &ReasoningBudget::openTag()
{
    static const std::string tag="<think>";
    return tag;
}

const std::string &ReasoningBudget::closeTag()
{
    static const std::string tag="</think>";
    return tag;
}

const std::string &ReasoningBudget::prefillText()
{
    static const std::string text="<think>\n\n</think>\n\n";
    return text;
}

const std::string &ReasoningBudget::closeText()
{
    static const std::string text="\n</think>\n\n";
    return text;
}

bool ReasoningBudget::enabled() const
{
    return m_budget.has_value()&&m_tokens.valid();
}

bool ReasoningBudget::wantsPrefill() const
{
    return enabled()&&m_budget.value()<=0;
}

void ReasoningBudget::observePrompt(const std::string &prompt)
{
    if(!enabled())
        return;

    size_t open=prompt.rfind(openTag());
    size_t close=prompt.rfind(closeTag());

    if(open!=std::string::npos&&(close==std::string::npos||open>close))
        m_inThinking=true;
    else if(close!=std::string::npos)
        m_inThinking=false;
    m_tail.clear();
}

bool ReasoningBudget::observe(int32_t token, const std::string &piece)
{
    if(!enabled())
        return false;

    if(token==m_tokens.open)
    {
        m_inThinking=true;
        m_tail.clear();
    }
    else if(token==m_tokens.close)
    {
        m_inThinking=false;
        m_tail.clear();
    }
    else
    {
        bool wasThinking=m_inThinking;

        scanText(piece);
        if(wasThinking&&m_inThinking)
            m_thinkingTokens++;
    }
    return m_inThinking&&m_thinkingTokens>=m_budget.value();
}

void ReasoningBudget::injected()
{
    m_inThinking=false;
    m_injections++;
    m_tail.clear();
}

void ReasoningBudget::scanText(const std::string &text)
{
    m_tail+=text;

    size_t open=m_tail.rfind(openTag());
    size_t close=m_tail.rfind(closeTag());
    size_t end=0;

    if(open!=std::string::npos&&(close==std::string::npos||open>close))
    {
        m_inThinking=true;
        end=open+openTag().size();
    }
    else if(close!=std::string::npos)
    {
        m_inThinking=false;
        end=close+closeTag().size();
    }
    m_tail.erase(0, end);

    // keep just enough to complete a tag split across pieces
    size_t keep=closeTag().size()-1;
    if(m_tail.size()>keep)
        m_tail.erase(0, m_tail.size()-keep);
}

}//namespace arbiterAI
