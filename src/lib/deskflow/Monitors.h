/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace deskflow {

//! One monitor of a computer, in the coordinates of its whole desktop (all monitors)
/*!
Deskflow moves the cursor over the rectangle that holds every monitor of a computer.
Where the monitors don't fill it (different heights, an offset) there is no screen,
and a cursor sent there is pulled to the nearest monitor by the system. Knowing the
monitors, the screen layout links only the parts of edges that have one.
*/
struct Monitor
{
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;
  bool operator==(const Monitor &) const = default;
};

//! At most this many monitors are sent or taken per computer
inline constexpr std::size_t kMaxMonitors = 16;

//! "x,y,w,h;x,y,w,h": how a client sends its monitors (kMsgDMonitors)
std::string formatMonitors(const std::vector<Monitor> &monitors);

//! What formatMonitors wrote; what isn't a sensible monitor is left out
std::vector<Monitor> parseMonitors(std::string_view text);

//! {"name":[[x,y,w,h],...],...}: every computer's monitors, for the app
std::string monitorsJson(const std::map<std::string, std::vector<Monitor>> &computers);

} // namespace deskflow
