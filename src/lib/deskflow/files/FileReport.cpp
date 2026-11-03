/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/files/FileReport.h"

#include "deskflow/ClipboardUtil.h"
#include "deskflow/ipc/CoreIpc.h"

#include <algorithm>
#include <array>

namespace deskflow::files {

using deskflow::clipboard::appendJsonString;

namespace {

//! The panel names a few, then says how many more
constexpr std::size_t kNames = 3;

constexpr auto kProgressInterval = std::chrono::milliseconds(250);

void appendSummary(std::string &json, const FileList &list)
{
  const auto files = list.fileCount();
  json += "{\"count\":" + std::to_string(files);
  json += ",\"folders\":" + std::to_string(list.entries.size() - files);
  json += ",\"bytes\":" + std::to_string(list.totalBytes());
  json += ",\"names\":[";
  const auto roots = list.roots();
  for (std::size_t i = 0; i < roots.size() && i < kNames; ++i) {
    if (i != 0)
      json += ',';
    appendJsonString(json, roots[i]);
  }
  json += "],\"roots\":" + std::to_string(roots.size()) + "}";
}

} // namespace

std::string reasonCode(const std::string &detail)
{
  // the phrases the file code uses (FileList, FileTransfer, FileChannel)
  static const std::array<std::pair<std::string_view, std::string_view>, 9> codes = {{
      {"larger than", "tooLarge"},
      {"too many", "tooLarge"},
      {"no user is logged on", "noUser"},
      {"changed", "changed"},
      {"damaged", "damaged"},
      {"disk", "diskFull"},
      {"clipboard sharing is off", "refused"},
      {"not taking part", "refused"},
      {"another copy was made", "superseded"},
  }};
  for (const auto &[phrase, code] : codes) {
    if (detail.find(phrase) != std::string::npos)
      return std::string(code);
  }
  return {};
}

std::string filesActivityJson(const FilesActivity &activity)
{
  std::string json = "{\"event\":";
  appendJsonString(json, activity.event);
  if (!activity.computer.empty()) {
    json += ",\"computer\":";
    appendJsonString(json, activity.computer);
  }
  if (!activity.detail.empty()) {
    if (const auto code = reasonCode(activity.detail); !code.empty()) {
      json += ",\"reason\":";
      appendJsonString(json, code);
    }
    json += ",\"detail\":";
    appendJsonString(json, activity.detail);
  }
  json += ",\"dropped\":[],\"formats\":[\"files\"]";
  if (activity.list != nullptr) {
    json += ",\"size\":" + std::to_string(activity.list->totalBytes());
    json += ",\"files\":";
    appendSummary(json, *activity.list);
  }
  json += '}';
  return json;
}

void reportFiles(const FilesActivity &activity)
{
  ipcSendToClient("clipboard", filesActivityJson(activity), true);
}

std::string
FileProgress::json(const std::string &direction, const std::string &computer, const FileList &list, std::uint64_t done)
{
  std::string json = "{\"direction\":";
  appendJsonString(json, direction);
  if (!computer.empty()) {
    json += ",\"computer\":";
    appendJsonString(json, computer);
  }
  json += ",\"count\":" + std::to_string(list.fileCount());
  json += ",\"bytes\":" + std::to_string(list.totalBytes());
  json += ",\"done\":" + std::to_string((std::min)(done, list.totalBytes()));
  json += '}';
  return json;
}

void FileProgress::update(
    const std::string &direction, const std::string &computer, const FileList &list, std::uint64_t done
)
{
  const auto now = std::chrono::steady_clock::now();
  if (m_active && now - m_last < kProgressInterval)
    return;
  m_active = true;
  m_last = now;
  ipcSendToClient("fileProgress", json(direction, computer, list, done), true);
}

void FileProgress::finish()
{
  if (!m_active)
    return;
  m_active = false;
  ipcSendToClient("fileProgress", "{\"done\":true}", true);
}

} // namespace deskflow::files
