/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardUtil.h"

#include "base/Endian.h"
#include "common/CoreSettings.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ipc/CoreIpc.h"

#include <algorithm>
#include <array>
#include <utility>

namespace deskflow::clipboard {

namespace {

using Format = IClipboard::Format;

constexpr std::array kAllFormats = {Format::Text, Format::HTML, Format::Bitmap, Format::PNG};

bool isImage(Format format)
{
  return format == Format::Bitmap || format == Format::PNG;
}

//! Text without CRs and without trailing line breaks
std::string normalizedText(const std::string &text)
{
  std::string result;
  result.reserve(text.size());
  for (const char c : text) {
    if (c != '\r')
      result += c;
  }
  while (!result.empty() && result.back() == '\n')
    result.pop_back();
  return result;
}

//! The formats in marshalled data, nothing for formats it doesn't have
std::array<std::optional<std::string>, kAllFormats.size()> unpack(const std::string &marshalled)
{
  Clipboard clipboard;
  clipboard.unmarshall(marshalled, 0);
  std::array<std::optional<std::string>, kAllFormats.size()> formats;
  clipboard.open(0);
  for (std::size_t i = 0; i < kAllFormats.size(); ++i) {
    if (clipboard.has(kAllFormats[i]))
      formats[i] = clipboard.get(kAllFormats[i]);
  }
  clipboard.close();
  return formats;
}

} // namespace

std::optional<std::string> snapshot(const IClipboard &source)
{
  Clipboard copy;
  if (!IClipboard::copy(&copy, &source))
    return std::nullopt;
  return copy.marshall();
}

bool hasContent(const std::string &marshalled)
{
  const auto formats = unpack(marshalled);
  return std::any_of(formats.begin(), formats.end(), [](const auto &data) { return data && !data->empty(); });
}

bool sameContent(const std::string &a, const std::string &b)
{
  if (a == b)
    return true;

  const auto left = unpack(a);
  const auto right = unpack(b);
  for (std::size_t i = 0; i < kAllFormats.size(); ++i) {
    if (left[i].has_value() != right[i].has_value())
      return false;
    if (!left[i])
      continue;
    const bool same =
        isImage(kAllFormats[i]) ? *left[i] == *right[i] : normalizedText(*left[i]) == normalizedText(*right[i]);
    if (!same)
      return false;
  }
  return true;
}

Fitted fitToLimit(const std::string &marshalled, std::size_t limitBytes)
{
  Fitted result;
  if (limitBytes == 0 || marshalled.size() <= limitBytes) {
    result.data = marshalled;
    result.fits = true;
    return result;
  }

  const auto formats = unpack(marshalled);

  // formats to leave out, least valuable first: the bitmap before the much smaller PNG
  // of the same image, the image before the HTML, the text never
  const std::array<std::vector<Format>, 3> attempts = {
      std::vector<Format>{Format::Bitmap}, std::vector<Format>{Format::Bitmap, Format::PNG},
      std::vector<Format>{Format::Bitmap, Format::PNG, Format::HTML}
  };
  for (const auto &drop : attempts) {
    Clipboard candidate;
    candidate.open(0);
    candidate.empty();
    for (std::size_t i = 0; i < kAllFormats.size(); ++i) {
      if (formats[i] && std::find(drop.begin(), drop.end(), kAllFormats[i]) == drop.end())
        candidate.add(kAllFormats[i], *formats[i]);
    }
    candidate.close();

    // in the order they were given up, for the log
    std::vector<Format> dropped;
    for (const auto format : drop) {
      const auto index =
          static_cast<std::size_t>(std::find(kAllFormats.begin(), kAllFormats.end(), format) - kAllFormats.begin());
      if (formats[index])
        dropped.push_back(format);
    }

    // nothing left to share is not a fit: an empty clipboard would erase the other side's
    auto data = candidate.marshall();
    if (dropped.size() < static_cast<std::size_t>(
                             std::count_if(formats.begin(), formats.end(), [](const auto &f) { return f.has_value(); })
                         ) &&
        data.size() <= limitBytes) {
      result.data = std::move(data);
      result.dropped = std::move(dropped);
      result.fits = true;
      return result;
    }
  }

  for (const auto format : {Format::Bitmap, Format::PNG, Format::HTML, Format::Text}) {
    const auto index =
        static_cast<std::size_t>(std::find(kAllFormats.begin(), kAllFormats.end(), format) - kAllFormats.begin());
    if (formats[index])
      result.dropped.push_back(format);
  }
  return result;
}

std::string formatNames(const std::vector<IClipboard::Format> &formats)
{
  std::string names;
  for (const auto format : formats) {
    if (!names.empty())
      names += ", ";
    switch (format) {
    case Format::Text:
      names += "text";
      break;
    case Format::HTML:
      names += "HTML";
      break;
    case Format::Bitmap:
      names += "bitmap";
      break;
    case Format::PNG:
      names += "png";
      break;
    default:
      names += "unknown";
    }
  }
  return names;
}

void appendJsonString(std::string &json, std::string_view value)
{
  static const char hex[] = "0123456789abcdef";
  json += '"';
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    switch (c) {
    case '"':
      json += "\\\"";
      break;
    case '\\':
      json += "\\\\";
      break;
    case '\n':
      json += "\\n";
      break;
    case '\r':
      json += "\\r";
      break;
    case '\t':
      json += "\\t";
      break;
    default:
      if (byte < 0x20) {
        json += "\\u00";
        json += hex[byte >> 4];
        json += hex[byte & 0xF];
      } else {
        json += c; // UTF-8 passes through
      }
    }
  }
  json += '"';
}

namespace {

void appendFormatList(std::string &json, const std::vector<Format> &formats)
{
  json += '[';
  for (std::size_t i = 0; i < formats.size(); ++i) {
    if (i != 0)
      json += ',';
    appendJsonString(json, formatNames({formats[i]}));
  }
  json += ']';
}

//! Width and height of the image, from the bitmap or the PNG
std::optional<std::pair<int64_t, int64_t>>
imageSize(const std::optional<std::string> &bitmap, const std::optional<std::string> &png)
{
  // a DIB: BITMAPINFOHEADER first, width and height after its size
  if (bitmap && bitmap->size() >= 12) {
    const auto width = static_cast<int32_t>(deskflow::fromLittleEndian<uint32_t>(bitmap->data() + 4));
    const auto height = static_cast<int32_t>(deskflow::fromLittleEndian<uint32_t>(bitmap->data() + 8));
    return std::pair<int64_t, int64_t>{width < 0 ? -int64_t{width} : width, height < 0 ? -int64_t{height} : height};
  }
  // a PNG: the 8-byte signature, then IHDR's length and type, then big-endian width and height
  if (png && png->size() >= 24 && png->compare(12, 4, "IHDR") == 0) {
    const auto bigEndian = [&](std::size_t at) {
      const auto *b = reinterpret_cast<const unsigned char *>(png->data() + at);
      return (uint32_t{b[0]} << 24) | (uint32_t{b[1]} << 16) | (uint32_t{b[2]} << 8) | uint32_t{b[3]};
    };
    return std::pair<int64_t, int64_t>{bigEndian(16), bigEndian(20)};
  }
  return std::nullopt;
}

bool isUtf8Continuation(char c)
{
  return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

//! The first maxChars characters (code points) of UTF-8 text
std::string utf8Prefix(const std::string &text, std::size_t maxChars, std::size_t &totalChars)
{
  totalChars = 0;
  std::size_t cut = text.size();
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (isUtf8Continuation(text[i]))
      continue;
    if (totalChars == maxChars)
      cut = std::min(cut, i);
    ++totalChars;
  }
  return text.substr(0, cut);
}

} // namespace

std::string activityJson(const Activity &activity, bool includeText)
{
  std::string json = "{\"event\":";
  appendJsonString(json, activity.event);
  if (!activity.computer.empty()) {
    json += ",\"computer\":";
    appendJsonString(json, activity.computer);
  }
  if (!activity.reason.empty()) {
    json += ",\"reason\":";
    appendJsonString(json, activity.reason);
  }
  json += ",\"dropped\":";
  appendFormatList(json, activity.dropped);

  std::vector<Format> present;
  if (!activity.marshalled.empty()) {
    const auto formats = unpack(activity.marshalled);
    for (std::size_t i = 0; i < kAllFormats.size(); ++i) {
      if (formats[i])
        present.push_back(kAllFormats[i]);
    }
    json += ",\"size\":" + std::to_string(activity.marshalled.size());

    if (const auto &text = formats[0]; text) {
      std::size_t chars = 0;
      const auto preview = utf8Prefix(*text, kPreviewChars, chars);
      json += ",\"chars\":" + std::to_string(chars);
      json += ",\"lines\":" + std::to_string(text->empty() ? 0 : std::count(text->begin(), text->end(), '\n') + 1);
      if (includeText) {
        json += ",\"text\":";
        appendJsonString(json, preview);
      }
    }

    if (const auto size = imageSize(formats[2], formats[3]); size) {
      json +=
          ",\"image\":{\"width\":" + std::to_string(size->first) + ",\"height\":" + std::to_string(size->second) + "}";
    }
  }
  json += ",\"formats\":";
  appendFormatList(json, present);
  json += '}';
  return json;
}

void reportActivity(const Activity &activity)
{
  const bool preview = CoreSettings::value(CoreSettings::Core::ClipboardPreview).toBool();
  ipcSendToClient("clipboard", activityJson(activity, preview), true);
}

void EchoGuard::applied(const std::string &marshalled)
{
  m_content = marshalled;
}

bool EchoGuard::isEcho(const IClipboard &local) const
{
  if (!m_content)
    return false;
  const auto current = snapshot(local);
  return current && sameContent(*m_content, *current);
}

void EchoGuard::clear()
{
  m_content.reset();
}

} // namespace deskflow::clipboard
