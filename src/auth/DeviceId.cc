#include "auth/DeviceId.h"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace posture
{

bool isUuid(const std::string &value)
{
    if (value.size() != 36)
        return false;

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(value[i]);
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
                return false;
        }
        else if (std::isxdigit(c) == 0)
        {
            return false;
        }
    }
    return true;
}

std::string hashDeviceId(const std::string &deviceId)
{
    std::string normalized = deviceId;
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char *>(normalized.data()),
           normalized.size(),
           digest.data());

    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const auto byte : digest)
    {
        hex.push_back(kHex[byte >> 4]);
        hex.push_back(kHex[byte & 0x0f]);
    }
    return hex;
}

}  // namespace posture
