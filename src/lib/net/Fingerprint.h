/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct Fingerprint
{
public:
  enum class Type
  {
    Invalid,
    Sha1,
    Sha256
  };

  using Bytes = std::vector<std::uint8_t>;

  Type type = Type::Invalid;
  Bytes data;

  bool isValid() const;

  bool operator==(const Fingerprint &other) const = default;

  //! The line written to a trusted fingerprints file ("v2:sha256:<hex>"), empty if invalid
  std::string toDbLine() const;
  static Fingerprint fromDbLine(std::string_view line);
  static std::string typeToString(Type type);
  static Type typeFromString(std::string_view type);

  //! Lowercase hex, optionally with a separator between bytes
  static std::string toHex(const Bytes &bytes, char separator = '\0');
  //! Bytes from hex digits (either case), as leniently as QByteArray::fromHex
  static Bytes fromHex(std::string_view hex);
};
