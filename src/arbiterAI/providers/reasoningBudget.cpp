#include "arbiterAI/providers/reasoningBudget.h"

namespace arbiterAI
{

namespace
{

bool isSpace(char c)
{
    return c==' '||c=='\n'||c=='\r'||c=='\t';
}

// True when text ends partway through a multi-byte UTF-8 character
bool endsMidCharacter(const std::string &text)
{
    for(size_t back=1; back<=text.size()&&back<=4; ++back)
    {
        unsigned char c=static_cast<unsigned char>(text[text.size()-back]);

        if((c&0xC0)==0x80)
            continue;

        size_t need=1;
        if((c>>5)==0x6)
            need=2;
        else if((c>>4)==0xE)
            need=3;
        else if((c>>3)==0x1E)
            need=4;
        return need>back;
    }
    return false;
}

}//namespace

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
    return m_budget.has_value()&&m_budget.value()>=0&&m_tokens.valid();
}

bool ReasoningBudget::wantsPrefill() const
{
    return enabled()&&m_budget.value()==0;
}

void ReasoningBudget::observePrompt(const std::string &prompt)
{
    if(!enabled())
        return;

    // inside only when the prompt ends with an open tag, e.g. a template
    // that opens the block itself
    size_t end=prompt.size();
    while(end>0&&isSpace(prompt[end-1]))
        end--;

    m_inThinking=end>=openTag().size()&&
        prompt.compare(end-openTag().size(), openTag().size(), openTag())==0;
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

    m_utf8Tail+=piece;
    if(m_utf8Tail.size()>4)
        m_utf8Tail.erase(0, m_utf8Tail.size()-4);

    // the close waits for a token that completes the character
    return m_inThinking&&m_thinkingTokens>=m_budget.value()&&!endsMidCharacter(m_utf8Tail);
}

void ReasoningBudget::injected()
{
    m_inThinking=false;
    m_injections++;
    m_tail.clear();
    m_utf8Tail.clear();
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
