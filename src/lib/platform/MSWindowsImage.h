/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

//! Clipboard images between the bitmap Deskflow shares and PNG, with Windows' own codecs (WIC)
/*!
A screenshot as a bitmap is 8 MB at 1920×1080 and 33 MB at 4K, more than the clipboard
limit; as PNG it is a fraction of that. These convert between the two forms of
`IClipboard::Format`: the bitmap (a BITMAPINFOHEADER, then BI_RGB pixels at 24 or 32
bits per pixel) and the bytes of a PNG file.
*/
namespace deskflow::platform::image {

//! The bitmap as a PNG file; nothing if it isn't a bitmap WIC can encode
/*!
The fourth byte of a 32-bit bitmap is ignored, as programs leave it undefined there.
*/
std::optional<std::string> bitmapToPng(std::string_view bitmap);

//! A PNG file as a bitmap: 32 bits per pixel, bottom-up; nothing if it can't be decoded
/*!
Programs that paste a bitmap ignore its fourth byte, so what is see-through in the
PNG is laid over white, as it would show on a page; the PNG keeps the transparency
for those that paste PNG.
*/
std::optional<std::string> pngToBitmap(std::string_view png);

//! Largest image either conversion takes, in pixels (about 16k × 16k)
inline constexpr unsigned long long kMaxPixels = 256ull * 1024 * 1024;

} // namespace deskflow::platform::image
