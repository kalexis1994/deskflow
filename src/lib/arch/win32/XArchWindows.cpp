/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/win32/XArchWindows.h"

#include "arch/Utf8.h"

std::string windowsErrorToString(DWORD error)
{
  LPWSTR buffer = nullptr; // Using FORMAT_MESSAGE_ALLOCATE_BUFFER
  DWORD size = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr
  );

  if (size == 0) {
    return "Unknown Windows error: " + std::to_string(error);
  }

  const auto message = deskflow::wideToUtf8(std::wstring_view(buffer, size));

  // Gotcha: Was allocated by FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER)
  LocalFree(buffer);

  return "[" + std::to_string(error) + "] " + deskflow::trimmed(message);
}
