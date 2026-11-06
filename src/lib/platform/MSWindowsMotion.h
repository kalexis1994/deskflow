/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

//! Whether a motion measured from the center is really where the cursor was before warping there
/*!
While the cursor is on another computer the primary keeps it at the center of the
screen and each motion is its distance from there. The hook runs on another thread,
so an event from before the warp can arrive after it: its position is near the edge
the cursor just crossed, which looks like a motion of half the screen and would
throw the cursor to the middle of the other computer.

Such a position isn't always right at the edge: the hook eats the event that reaches
the jump zone, so a fast cursor stays behind by as much as it moved in one event. So
anything past half the way to an edge counts. No mouse moves that far in one event,
and losing a real one wouldn't be noticed.

\p dx and \p dy are the motion; \p x, \p y, \p w and \p h the desktop (all monitors);
\p xCenter and \p yCenter the point the cursor is kept at.
*/
inline bool
isPreWarpMotion(int32_t dx, int32_t dy, int32_t x, int32_t y, int32_t w, int32_t h, int32_t xCenter, int32_t yCenter)
{
  const auto pastHalfway = [](int32_t motion, int32_t room) { return 2 * motion > room; };
  return pastHalfway(-dx, xCenter - x) || pastHalfway(dx, x + w - xCenter) || pastHalfway(-dy, yCenter - y) ||
         pastHalfway(dy, y + h - yCenter);
}
