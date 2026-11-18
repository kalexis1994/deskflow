/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

//! A rectangle on the desktop: right and bottom are just past it, as in RECT
struct CaptureRect
{
  long left = 0;
  long top = 0;
  long right = 0;
  long bottom = 0;
};

//! Whether a program has taken the mouse for itself, as a first-person game does
/*!
Such a game hides the cursor and confines it to a small part of the screen
(`ClipCursor`), then reads the mouse's own motion, recentering the cursor every frame.
Positions are useless to it; only relative motion turns the camera. Either sign alone
is common (a video player hides an idle cursor; a strategy game confines a visible
one), so it takes both.

And the confinement must lie in the window in front, as a game confines the cursor to
itself. Both signs once came from something else, with no game in front, and the mouse
stayed on a computer nobody was playing on.

\p clip is where the cursor is confined, \p screen the whole desktop (all monitors),
\p window the window in front (none: nullptr); \p cursorShown whether the cursor is visible.
*/
inline bool
isMouseCaptured(const CaptureRect &clip, const CaptureRect &screen, const CaptureRect *window, bool cursorShown)
{
  if (cursorShown || window == nullptr)
    return false;
  const bool confined =
      clip.left > screen.left || clip.top > screen.top || clip.right < screen.right || clip.bottom < screen.bottom;
  const bool empty = clip.right <= clip.left || clip.bottom <= clip.top;
  // a window's frame and shadow reach a few pixels past what it confines the cursor to,
  // and a full-screen one can reach past its monitor
  constexpr long kSlack = 16;
  const bool inWindow = clip.left >= window->left - kSlack && clip.top >= window->top - kSlack &&
                        clip.right <= window->right + kSlack && clip.bottom <= window->bottom + kSlack;
  return confined && !empty && inWindow;
}
