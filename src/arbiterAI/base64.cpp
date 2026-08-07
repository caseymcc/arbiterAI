#include "arbiterAI/base64.h"

namespace arbiterAI
{

namespace
{

/// Reverse lookup for the standard base64 alphabet.
/// -1 = invalid, -2 = ignorable whitespace, -3 = padding.
int decodeChar(unsigned char c)
{
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    if(c=='=') return -3;
    if(c==' '||c=='\t'||c=='\n'||c=='\r') return -2;
    return -1;
}

} // namespace

bool base64Decode(const std::string &input, std::vector<uint8_t> &out)
{
    out.clear();
    out.reserve((input.size()/4)*3);

    uint32_t accumulator=0;
    int bits=0;
    bool padded=false;

    for(char ch:input)
    {
        int value=decodeChar(static_cast<unsigned char>(ch));

        if(value==-2)
        {
            continue;
        }
        if(value==-3)
        {
            padded=true;
            continue;
        }
        if(value<0||padded)
        {
            // Invalid character, or data after padding.
            return false;
        }

        accumulator=(accumulator<<6)|static_cast<uint32_t>(value);
        bits+=6;

        if(bits>=8)
        {
            bits-=8;
            out.push_back(static_cast<uint8_t>((accumulator>>bits)&0xFF));
        }
    }

    // A valid stream leaves fewer than 8 bits, and those must all be zero.
    if(bits>=8||(accumulator&((1u<<bits)-1))!=0)
    {
        return false;
    }
    return true;
}

} // namespace arbiterAI
