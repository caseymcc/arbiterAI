#include "arbiterAI/thinkTags.h"

#include <algorithm>

namespace arbiterAI
{

namespace
{

const char *const kThinkOpen="<think>";
const char *const kThinkClose="</think>";

/// Length of the longest suffix of `text` that is a proper prefix of `tag`.
/// Those bytes are held back so a tag split across chunks isn't emitted as
/// content before we can tell what it is.
size_t partialTagSuffixLen(const std::string &text, const std::string &tag)
{
    size_t maxLen=std::min(text.size(), tag.size()-1);
    for(size_t len=maxLen; len>0; --len)
    {
        if(text.compare(text.size()-len, len, tag, 0, len)==0)
        {
            return len;
        }
    }
    return 0;
}

} // namespace

ThinkTagParseResult parseThinkTags(const std::string &text)
{
    ThinkTagParseResult result;
    const std::string open(kThinkOpen);
    const std::string close(kThinkClose);

    size_t pos=0;
    while(pos<text.size())
    {
        size_t openPos=text.find(open, pos);
        if(openPos==std::string::npos)
        {
            result.content+=text.substr(pos);
            break;
        }

        result.content+=text.substr(pos, openPos-pos);

        size_t closePos=text.find(close, openPos+open.size());
        if(closePos==std::string::npos)
        {
            if(!result.reasoningContent.empty()) result.reasoningContent+="\n";
            result.reasoningContent+=text.substr(openPos+open.size());
            break;
        }

        if(!result.reasoningContent.empty()) result.reasoningContent+="\n";
        result.reasoningContent+=text.substr(openPos+open.size(), closePos-openPos-open.size());
        pos=closePos+close.size();
    }

    // The block is normally followed by blank lines; don't hand those to the client.
    size_t firstReal=result.content.find_first_not_of(" \t\r\n");
    if(firstReal==std::string::npos)
    {
        result.content.clear();
    }
    else if(firstReal>0)
    {
        result.content.erase(0, firstReal);
    }

    return result;
}

void ThinkTagStreamParser::feed(const std::string &chunk, std::string &contentOut, std::string &reasoningOut)
{
    m_buffer+=chunk;

    const std::string open(kThinkOpen);
    const std::string close(kThinkClose);
    size_t reasoningStart=reasoningOut.size();

    while(true)
    {
        const std::string &marker=m_inThink?close:open;
        std::string &out=m_inThink?reasoningOut:contentOut;

        size_t pos=m_buffer.find(marker);
        if(pos!=std::string::npos)
        {
            out+=m_buffer.substr(0, pos);
            m_buffer.erase(0, pos+marker.size());
            m_inThink=!m_inThink;
            continue;
        }

        size_t hold=partialTagSuffixLen(m_buffer, marker);
        out+=m_buffer.substr(0, m_buffer.size()-hold);
        m_buffer.erase(0, m_buffer.size()-hold);
        break;
    }

    m_reasoning+=reasoningOut.substr(reasoningStart);
}

void ThinkTagStreamParser::flush(std::string &contentOut, std::string &reasoningOut)
{
    if(m_buffer.empty()) return;

    if(m_inThink)
    {
        reasoningOut+=m_buffer;
        m_reasoning+=m_buffer;
    }
    else
    {
        contentOut+=m_buffer;
    }
    m_buffer.clear();
}

} // namespace arbiterAI
