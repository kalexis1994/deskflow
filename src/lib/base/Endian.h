/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace deskflow {

//! Reads a little-endian unsigned integer from unaligned bytes, on any host
template <typename T> T fromLittleEndian(const void *source)
{
  static_assert(std::is_unsigned_v<T>);
  const auto *bytes = static_cast<const std::uint8_t *>(source);
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i)
    value = static_cast<T>(value | (static_cast<T>(bytes[i]) << (8 * i)));
  return value;
}

//! Writes an unsigned integer as little-endian bytes to unaligned memory, on any host
template <typename T> void toLittleEndian(T value, void *destination)
{
  static_assert(std::is_unsigned_v<T>);
  auto *bytes = static_cast<std::uint8_t *>(destination);
  for (std::size_t i = 0; i < sizeof(T); ++i)
    bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

} // namespace deskflow
