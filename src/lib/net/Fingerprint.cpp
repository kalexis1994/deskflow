/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Fingerprint.h"
#include "base/Log.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace {

const auto kTypeSha1 = "sha1";
const auto kTypeSha256 = "sha256";
const auto kTypeInvalid = "invalid";

int hexValue(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

std::string toLower(std::string_view text)
{
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

} // namespace

bool Fingerprint::isValid() const
{
  switch (type) {
  case Type::Sha1:
    return data.size() == 20;
  case Type::Sha256:
    return data.size() == 32;
  default:
    return false;
  }
}

std::string Fingerprint::toDbLine() const
{
  if (!isValid()) {
    LOG_INFO("unable to parse invalid db line");
    return {};
  }
  return "v2:" + typeToString(type) + ":" + toHex(data);
}

Fingerprint Fingerprint::fromDbLine(std::string_view line)
{
  Fingerprint result;

  if (line.empty())
    return result;

  if (line.starts_with("v2:")) {
    const auto rest = line.substr(3);
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos || rest.find(':', colon + 1) != std::string_view::npos)
      return result;
    result.type = typeFromString(rest.substr(0, colon));
    result.data = fromHex(rest.substr(colon + 1));
  } else {
    // v1 fallback: a SHA1 written as colon-separated hex bytes
    static const auto kSha1ColonCount = 19;
    static const auto kSha1HexCharCount = 40;
    static const auto kSha1ExpectedSize = kSha1HexCharCount + kSha1ColonCount;
    const bool wrongSize = line.size() != kSha1ExpectedSize;
    if (const bool badColonCount = std::count(line.begin(), line.end(), ':') != kSha1ColonCount;
        wrongSize || badColonCount)
      return result;
    result.type = Type::Sha1;
    std::string hex;
    std::copy_if(line.begin(), line.end(), std::back_inserter(hex), [](char c) { return c != ':'; });
    result.data = fromHex(hex);
  }

  return result;
}

std::string Fingerprint::typeToString(Type type)
{
  switch (type) {
  case Type::Sha1:
    return kTypeSha1;
  case Type::Sha256:
    return kTypeSha256;
  default:
    return kTypeInvalid;
  }
}

Fingerprint::Type Fingerprint::typeFromString(std::string_view type)
{
  const auto t = toLower(type);
  if (t == kTypeSha1)
    return Type::Sha1;
  if (t == kTypeSha256)
    return Type::Sha256;
  return Type::Invalid;
}

std::string Fingerprint::toHex(const Bytes &bytes, char separator)
{
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 3);
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0 && separator != '\0')
      result += separator;
    result += digits[bytes[i] >> 4];
    result += digits[bytes[i] & 0xF];
  }
  return result;
}

Fingerprint::Bytes Fingerprint::fromHex(std::string_view hex)
{
  // As QByteArray::fromHex, which wrote and read these files before: non-hex characters
  // are skipped and digits pair up from the end, so an odd digit count leaves a lone
  // leading digit as its own byte.
  Bytes reversed;
  bool lowNibble = true;
  for (auto it = hex.rbegin(); it != hex.rend(); ++it) {
    const int value = hexValue(*it);
    if (value < 0)
      continue;
    if (lowNibble)
      reversed.push_back(static_cast<std::uint8_t>(value));
    else
      reversed.back() = static_cast<std::uint8_t>(reversed.back() | (value << 4));
    lowNibble = !lowNibble;
  }
  return Bytes(reversed.rbegin(), reversed.rend());
}
