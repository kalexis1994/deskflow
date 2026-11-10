/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

//! Whether a program has taken the mouse for itself, as a first-person game does
/*!
Such a game hides the cursor and confines it to a small part of the screen
(`ClipCursor`), then reads the mouse's own motion, recentering the cursor every frame.
Positions are useless to it; only relative motion turns the camera. Either sign alone
is common (a video player hides an idle cursor; a strategy game confines a visible
one), so it takes both.

\p clip is where the cursor is confined, \p screen the whole desktop (all monitors),
as left, top, right, bottom; \p cursorShown whether the cursor is visible.
*/
inline bool isMouseCaptured(
    long clipLeft, long clipTop, long clipRight, long clipBottom, long screenLeft, long screenTop, long screenRight,
    long screenBottom, bool cursorShown
)
{
  if (cursorShown)
    return false;
  const bool confined =
      clipLeft > screenLeft || clipTop > screenTop || clipRight < screenRight || clipBottom < screenBottom;
  return confined && clipRight > clipLeft && clipBottom > clipTop;
}
