/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2014 - 2016, 2026 Symless Ltd
 * SPDX-FileCopyrightText: (C) 2014 Ryan Chapman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboard.h"

#include <cstddef>
#include <cstdint>

class OSXClipboardBMPConverter : public IOSXClipboardConverter
{
public:
  OSXClipboardBMPConverter() = default;
  ~OSXClipboardBMPConverter() override = default;

  IClipboard::Format getFormat() const override;
  CFStringRef getOSXFormat() const override;

  std::string fromIClipboard(const std::string &) const override;
  std::string toIClipboard(const std::string &) const override;

  static std::string convertString(const std::string &data, CFStringEncoding fromEncoding, CFStringEncoding toEncoding);

private:
  static constexpr uint32_t kBiBitfields = 3;
  static constexpr uint32_t kBiAlphabitfields = 6;
  static constexpr uint32_t kBmpHeaderDIBPad = 10;
  static constexpr uint32_t kBmpFileHeaderSize = 14;
  static constexpr uint32_t kFallbackPixelOffset = 40;

  static uint32_t dibPixelOffset(const uint8_t *dib, size_t dibSize);
};
