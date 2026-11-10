/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardPNGConverter.h"

#include <cstring>

UINT MSWindowsClipboardPNGConverter::format()
{
  static const UINT png = RegisterClipboardFormatW(L"PNG");
  return png;
}

std::string_view MSWindowsClipboardPNGConverter::trimmed(std::string_view data)
{
  // the last chunk: its length (0), "IEND", then a 4-byte CRC
  const auto end = data.rfind("IEND");
  if (end == std::string_view::npos || data.size() - end < 8)
    return data;
  return data.substr(0, end + 8);
}

IClipboard::Format MSWindowsClipboardPNGConverter::getFormat() const
{
  return IClipboard::Format::PNG;
}

UINT MSWindowsClipboardPNGConverter::getWin32Format() const
{
  return format();
}

HANDLE MSWindowsClipboardPNGConverter::fromIClipboard(const std::string &data) const
{
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, data.size());
  if (memory == nullptr)
    return nullptr;
  auto *bytes = static_cast<char *>(GlobalLock(memory));
  if (bytes == nullptr) {
    GlobalFree(memory);
    return nullptr;
  }
  std::memcpy(bytes, data.data(), data.size());
  GlobalUnlock(memory);
  return memory;
}

std::string MSWindowsClipboardPNGConverter::toIClipboard(HANDLE data) const
{
  const auto *bytes = static_cast<const char *>(GlobalLock(data));
  if (bytes == nullptr)
    return {};
  std::string png(trimmed(std::string_view(bytes, GlobalSize(data))));
  GlobalUnlock(data);
  return png;
}
