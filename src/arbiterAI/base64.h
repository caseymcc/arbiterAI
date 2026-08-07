#ifndef _ARBITERAI_BASE64_H_
#define _ARBITERAI_BASE64_H_

#include <cstdint>
#include <string>
#include <vector>

namespace arbiterAI
{

/// Decode standard base64 (RFC 4648).  Whitespace is ignored so wrapped
/// payloads decode as-is.  Returns false on any invalid character or a
/// truncated final quantum; `out` is undefined in that case.
bool base64Decode(const std::string &input, std::vector<uint8_t> &out);

} // namespace arbiterAI

#endif//_ARBITERAI_BASE64_H_
