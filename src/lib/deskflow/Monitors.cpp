/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/Monitors.h"

#include <array>
#include <charconv>

namespace deskflow {

namespace {

constexpr int32_t kMaxCoordinate = 1'000'000;
constexpr int32_t kMaxSize = 100'000;

bool parseNumber(std::string_view text, int32_t &value)
{
  const auto [rest, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc() && rest == text.data() + text.size();
}

void appendJsonString(std::string &json, std::string_view value)
{
  json += '"';
  for (const char c : value) {
    if (c == '"' || c == '\\') {
      json += '\\';
      json += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      static const char hex[] = "0123456789abcdef";
      json += "\\u00";
      json += hex[(c >> 4) & 0xF];
      json += hex[c & 0xF];
    } else {
      json += c;
    }
  }
  json += '"';
}

} // namespace

std::string formatMonitors(const std::vector<Monitor> &monitors)
{
  std::string text;
  for (std::size_t i = 0; i < monitors.size() && i < kMaxMonitors; ++i) {
    const auto &m = monitors[i];
    if (!text.empty())
      text += ';';
    text += std::to_string(m.x) + ',' + std::to_string(m.y) + ',' + std::to_string(m.w) + ',' + std::to_string(m.h);
  }
  return text;
}

std::vector<Monitor> parseMonitors(std::string_view text)
{
  std::vector<Monitor> monitors;
  while (!text.empty() && monitors.size() < kMaxMonitors) {
    const auto end = text.find(';');
    auto item = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);

    std::array<int32_t, 4> values = {};
    bool ok = true;
    for (std::size_t i = 0; i < values.size() && ok; ++i) {
      const auto comma = item.find(',');
      if ((i < values.size() - 1) == (comma == std::string_view::npos)) {
        ok = false;
        break;
      }
      ok = parseNumber(item.substr(0, comma), values[i]);
      item = comma == std::string_view::npos ? std::string_view() : item.substr(comma + 1);
    }
    const Monitor m{values[0], values[1], values[2], values[3]};
    if (ok && m.w > 0 && m.h > 0 && m.w <= kMaxSize && m.h <= kMaxSize && m.x > -kMaxCoordinate &&
        m.x < kMaxCoordinate && m.y > -kMaxCoordinate && m.y < kMaxCoordinate)
      monitors.push_back(m);
  }
  return monitors;
}

std::string monitorsJson(const std::map<std::string, std::vector<Monitor>> &computers)
{
  std::string json = "{";
  bool first = true;
  for (const auto &[name, monitors] : computers) {
    if (!first)
      json += ',';
    first = false;
    appendJsonString(json, name);
    json += ":[";
    for (std::size_t i = 0; i < monitors.size(); ++i) {
      const auto &m = monitors[i];
      if (i != 0)
        json += ',';
      json += '[' + std::to_string(m.x) + ',' + std::to_string(m.y) + ',' + std::to_string(m.w) + ',' +
              std::to_string(m.h) + ']';
    }
    json += ']';
  }
  json += '}';
  return json;
}

} // namespace deskflow
