/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/OsDetection.h"

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace deskflow::platform {

inline bool isWayland()
{
  const char *session = std::getenv("XDG_SESSION_TYPE");
  return session != nullptr && std::string_view(session) == "wayland";
}

/**
 * @brief isWindows
 * @return Returns true if we are running on windows
 */
inline bool isWindows()
{
#if defined(DESKFLOW_OS_WIN)
  return true;
#else
  return false;
#endif
}

/**
 * @brief isMac
 * @return Returns true if we are running on mac os
 */
inline bool isMac()
{
#if defined(DESKFLOW_OS_MACOS)
  return true;
#else
  return false;
#endif
}

/**
 * @brief isFlatpak
 * @return Returns true if we are running as flatpak
 */
inline bool isFlatpak()
{
  std::error_code ec;
  return std::filesystem::exists("/.flatpak-info", ec);
}

/**
 * @brief isSnap
 * @return Returns true if we are running as a snap
 */
inline bool isSnap()
{
  return std::getenv("SNAP") != nullptr;
}

/**
 * @brief isSandboxed
 * @return Returns true if we are running from in a known sandbox.
 */
inline bool isSandboxed()
{
  return isFlatpak() || isSnap();
}

} // namespace deskflow::platform
