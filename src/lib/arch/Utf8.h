/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

// Text in the core is UTF-8 in std::string. These helpers convert at the edges:
// to UTF-16 for Windows APIs, and to std::filesystem::path so that paths with
// non-ASCII characters open correctly on Windows too.

#include "arch/OsDetection.h"

#include <filesystem>
#include <string>
#include <string_view>

#if defined(DESKFLOW_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace deskflow {

#if defined(DESKFLOW_OS_WIN)

inline std::wstring utf8ToWide(std::string_view text)
{
  if (text.empty())
    return {};
  const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
  return result;
}

inline std::string wideToUtf8(std::wstring_view text)
{
  if (text.empty())
    return {};
  const auto size =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr
  );
  return result;
}

#endif

//! A UTF-8 path as a filesystem path (on Windows a plain std::string would be read in the ANSI code page)
inline std::filesystem::path utf8Path(std::string_view path)
{
  return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

inline std::string pathToUtf8(const std::filesystem::path &path)
{
  const auto u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

//! Copy with leading and trailing whitespace removed
inline std::string trimmed(std::string_view text)
{
  const auto first = text.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r\n\f\v");
  return std::string(text.substr(first, last - first + 1));
}

} // namespace deskflow
