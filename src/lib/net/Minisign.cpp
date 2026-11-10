/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/Minisign.h"

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace deskflow::minisign {

namespace {

constexpr std::size_t kKeyIdSize = 8;
constexpr std::size_t kPublicKeySize = 32;
constexpr std::size_t kSignatureSize = 64;
constexpr std::string_view kTrustedPrefix = "trusted comment: ";

std::vector<std::string_view> lines(std::string_view text)
{
  std::vector<std::string_view> result;
  while (!text.empty()) {
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    result.push_back(line);
    if (end == std::string_view::npos)
      break;
    text.remove_prefix(end + 1);
  }
  return result;
}

//! The text itself, or what its base64 decodes to, as Tauri stores it
std::optional<std::string> asText(std::string_view text)
{
  if (text.starts_with("untrusted comment:"))
    return std::string(text);
  auto decoded = decodeBase64(text);
  if (!decoded || !decoded->starts_with("untrusted comment:"))
    return std::nullopt;
  return decoded;
}

bool verifyEd25519(const std::string &key, std::string_view signature, std::string_view message)
{
  using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
  using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  const Key pkey(
      EVP_PKEY_new_raw_public_key(
          EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(key.data()), key.size()
      ),
      EVP_PKEY_free
  );
  const Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!pkey || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, pkey.get()) != 1)
    return false;
  return EVP_DigestVerify(
             context.get(), reinterpret_cast<const unsigned char *>(signature.data()), signature.size(),
             reinterpret_cast<const unsigned char *>(message.data()), message.size()
         ) == 1;
}

std::optional<std::string> blake2b512(std::string_view data)
{
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  if (EVP_Digest(data.data(), data.size(), digest.data(), &size, EVP_blake2b512(), nullptr) != 1)
    return std::nullopt;
  return std::string(reinterpret_cast<const char *>(digest.data()), size);
}

} // namespace

std::optional<std::string> decodeBase64(std::string_view text)
{
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  bool padding = false;
  for (const char c : text) {
    int value = -1;
    if (c >= 'A' && c <= 'Z')
      value = c - 'A';
    else if (c >= 'a' && c <= 'z')
      value = c - 'a' + 26;
    else if (c >= '0' && c <= '9')
      value = c - '0' + 52;
    else if (c == '+')
      value = 62;
    else if (c == '/')
      value = 63;
    else if (c == '=') {
      padding = true;
      continue;
    } else if (c == '\n' || c == '\r') {
      continue;
    } else {
      return std::nullopt;
    }
    if (padding)
      return std::nullopt; // data after the padding
    buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

std::optional<std::string> verify(std::string_view publicKey, std::string_view signature, std::string_view data)
{
  const auto keyText = asText(publicKey);
  const auto signatureText = asText(signature);
  if (!keyText || !signatureText)
    return std::nullopt;

  const auto keyLines = lines(*keyText);
  if (keyLines.size() < 2)
    return std::nullopt;
  const auto key = decodeBase64(keyLines[1]);
  if (!key || key->size() != 2 + kKeyIdSize + kPublicKeySize || !key->starts_with("Ed"))
    return std::nullopt;

  const auto signatureLines = lines(*signatureText);
  if (signatureLines.size() < 4 || !signatureLines[2].starts_with(kTrustedPrefix))
    return std::nullopt;
  const auto first = decodeBase64(signatureLines[1]);
  const auto global = decodeBase64(signatureLines[3]);
  if (!first || first->size() != 2 + kKeyIdSize + kSignatureSize || !global || global->size() != kSignatureSize)
    return std::nullopt;

  // the same key
  if (first->compare(2, kKeyIdSize, *key, 2, kKeyIdSize) != 0)
    return std::nullopt;

  const auto algorithm = first->substr(0, 2);
  std::optional<std::string> message;
  if (algorithm == "ED")
    message = blake2b512(data);
  else if (algorithm == "Ed")
    message = std::string(data);
  if (!message)
    return std::nullopt;

  const auto publicBytes = key->substr(2 + kKeyIdSize);
  const auto firstSignature = std::string_view(*first).substr(2 + kKeyIdSize);
  if (!verifyEd25519(publicBytes, firstSignature, *message))
    return std::nullopt;

  // the trusted comment is signed along with the first signature
  auto trusted = std::string(signatureLines[2].substr(kTrustedPrefix.size()));
  if (!verifyEd25519(publicBytes, *global, std::string(firstSignature) + trusted))
    return std::nullopt;
  return trusted;
}

} // namespace deskflow::minisign
