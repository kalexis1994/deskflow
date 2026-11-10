/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

#include <string_view>

//! The "PNG" format browsers, Office and the Snipping Tool put on the clipboard, as is
class MSWindowsClipboardPNGConverter : public IMSWindowsClipboardConverter
{
public:
  //! The registered "PNG" format
  static UINT format();

  //! The PNG file in `data`, without what follows its end (a memory block can be larger)
  static std::string_view trimmed(std::string_view data);

  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;
};
