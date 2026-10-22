/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardUtil.h"

#include "deskflow/Clipboard.h"

#include <algorithm>
#include <array>

namespace deskflow::clipboard {

namespace {

using Format = IClipboard::Format;

constexpr std::array kAllFormats = {Format::Text, Format::HTML, Format::Bitmap};

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
    const bool same = kAllFormats[i] == Format::Bitmap ? *left[i] == *right[i]
                                                       : normalizedText(*left[i]) == normalizedText(*right[i]);
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

  // formats to leave out, least valuable first
  const std::array<std::vector<Format>, 2> attempts = {
      std::vector<Format>{Format::Bitmap}, std::vector<Format>{Format::Bitmap, Format::HTML}
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

  for (const auto format : {Format::Bitmap, Format::HTML, Format::Text}) {
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
    default:
      names += "unknown";
    }
  }
  return names;
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
