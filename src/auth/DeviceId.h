#pragma once

#include <string>

namespace posture
{

// True for the canonical 8-4-4-4-12 hex UUID form, in either letter case.
bool isUuid(const std::string &value);

// Returns the lowercase hex SHA-256 digest of the install id, which is what
// users.device_hash stores. The id is lowercased first so the same UUID in a
// different case maps to the same user. The raw id is never persisted.
std::string hashDeviceId(const std::string &deviceId);

}  // namespace posture
