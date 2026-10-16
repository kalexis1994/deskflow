/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2014 - 2016, 2023 - 2026 Symless Ltd
 * SPDX-FileCopyrightText: (C) 2014 Ryan Chapman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardBMPConverter.h"

#include "base/Endian.h"
#include "base/Log.h"

uint32_t OSXClipboardBMPConverter::dibPixelOffset(const uint8_t *dib, size_t dibSize)
{
  uint32_t pixelOffset = 0;
  if (dibSize >= 16) {
    const auto biSize = deskflow::fromLittleEndian<uint32_t>(dib);
    if (biSize >= 12 && biSize <= dibSize) {
      pixelOffset = biSize;
      if (biSize >= kFallbackPixelOffset) {
        const auto biBitCount = deskflow::fromLittleEndian<uint16_t>(dib + 14);
        const auto biCompression = deskflow::fromLittleEndian<uint32_t>(dib + 16);
        if (biSize == kFallbackPixelOffset) {
          if (biCompression == kBiBitfields) {
            pixelOffset += 12;
          } else if (biCompression == kBiAlphabitfields) {
            pixelOffset += 16;
          }
        }
        if (biBitCount > 0 && biBitCount <= 8) {
          const auto biClrUsed = deskflow::fromLittleEndian<uint32_t>(dib + 32);
          const auto numColors = biClrUsed != 0 ? biClrUsed : (1u << biBitCount);
          pixelOffset += numColors * 4;
        }
      }
    }
  }
  return pixelOffset;
}

IClipboard::Format OSXClipboardBMPConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

CFStringRef OSXClipboardBMPConverter::getOSXFormat() const
{
  return CFSTR("com.microsoft.bmp");
}

std::string OSXClipboardBMPConverter::fromIClipboard(const std::string &bmp) const
{
  if (bmp.size() < 4) {
    LOG_DEBUG("rejecting clipboard dib, too small to wrap as bmp, size: %zu bytes", bmp.size());
    return std::string();
  }

  auto pixelOffset = dibPixelOffset(reinterpret_cast<const uint8_t *>(bmp.data()), bmp.size());
  if (pixelOffset == 0) {
    pixelOffset = kFallbackPixelOffset;
  }

  uint8_t header[kBmpFileHeaderSize];
  header[0] = 'B';
  header[1] = 'M';
  deskflow::toLittleEndian<uint32_t>(static_cast<uint32_t>(kBmpFileHeaderSize + bmp.size()), header + 2);
  deskflow::toLittleEndian<uint16_t>(uint16_t{0}, header + 6);
  deskflow::toLittleEndian<uint16_t>(uint16_t{0}, header + 8);
  deskflow::toLittleEndian<uint32_t>(kBmpFileHeaderSize + pixelOffset, header + kBmpHeaderDIBPad);
  return std::string(reinterpret_cast<const char *>(header), kBmpFileHeaderSize) + bmp;
}

std::string OSXClipboardBMPConverter::toIClipboard(const std::string &bmp) const
{
  if (bmp.size() <= kBmpFileHeaderSize) {
    LOG_DEBUG("rejecting clipboard bmp, too small to parse, size: %zu bytes", bmp.size());
    return std::string();
  }
  const auto *raw = reinterpret_cast<const uint8_t *>(bmp.data());
  if (raw[0] != 'B' || raw[1] != 'M') {
    LOG_DEBUG("rejecting clipboard bmp, missing bm magic");
    return std::string();
  }

  // macOS auto-promotes pasteboard images as 32-bit BITMAPV5HEADER + BI_BITFIELDS,
  // so the full DIB (extended header, masks, palette) must reach the receiver. The
  // earlier code truncated to 40 bytes and dropped the BGRA bitfield masks.
  const auto offset = deskflow::fromLittleEndian<uint32_t>(raw + kBmpHeaderDIBPad);
  const auto naturalOffset = dibPixelOffset(raw + kBmpFileHeaderSize, bmp.size() - kBmpFileHeaderSize);
  std::string result;
  const bool hasGap = naturalOffset != 0 && offset > kBmpFileHeaderSize + naturalOffset && offset <= bmp.size();
  if (hasGap) {
    result = bmp.substr(kBmpFileHeaderSize, naturalOffset) + bmp.substr(offset);
  } else {
    result = bmp.substr(kBmpFileHeaderSize);
  }
  return result;
}
