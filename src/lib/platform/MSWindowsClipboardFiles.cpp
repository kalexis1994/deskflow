/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFiles.h"

#include "base/Log.h"
#include "platform/MSWindowsClipboard.h"

#include <ShlObj.h>

#include <cstring>

namespace fs = std::filesystem;

namespace {

constexpr DWORD kDropEffectCopy = 1; // DROPEFFECT_COPY

} // namespace

UINT MSWindowsClipboardFiles::dropEffectFormat()
{
  static const UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
  return format;
}

HGLOBAL MSWindowsClipboardFiles::makeDropFiles(const std::vector<fs::path> &files)
{
  if (files.empty())
    return nullptr;

  // DROPFILES, then each path with its terminator, then one more terminator
  std::vector<std::wstring> paths;
  std::size_t characters = 1;
  for (const auto &file : files) {
    auto path = fs::path(file).make_preferred().native();
    if (path.empty())
      continue;
    characters += path.size() + 1;
    paths.push_back(std::move(path));
  }
  if (paths.empty())
    return nullptr;

  const auto size = sizeof(DROPFILES) + characters * sizeof(wchar_t);
  HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
  if (data == nullptr)
    return nullptr;
  auto *bytes = static_cast<std::byte *>(GlobalLock(data));
  if (bytes == nullptr) {
    GlobalFree(data);
    return nullptr;
  }

  auto *header = reinterpret_cast<DROPFILES *>(bytes);
  header->pFiles = sizeof(DROPFILES);
  header->fWide = TRUE;
  auto *out = reinterpret_cast<wchar_t *>(bytes + sizeof(DROPFILES));
  for (const auto &path : paths) {
    std::memcpy(out, path.c_str(), path.size() * sizeof(wchar_t));
    out += path.size() + 1; // zeroed: the terminator is there
  }
  GlobalUnlock(data);
  return data;
}

std::vector<fs::path> MSWindowsClipboardFiles::readDropFiles(HDROP drop)
{
  std::vector<fs::path> files;
  if (drop == nullptr)
    return files;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < count; ++i) {
    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
    if (length == 0)
      continue;
    std::wstring path(length + 1, L'\0');
    const UINT copied = DragQueryFileW(drop, i, path.data(), length + 1);
    path.resize(copied);
    if (!path.empty())
      files.emplace_back(std::move(path));
  }
  return files;
}

HGLOBAL MSWindowsClipboardFiles::makeDropEffect(DWORD effect)
{
  HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
  if (data == nullptr)
    return nullptr;
  if (auto *value = static_cast<DWORD *>(GlobalLock(data))) {
    *value = effect;
    GlobalUnlock(data);
    return data;
  }
  GlobalFree(data);
  return nullptr;
}

std::optional<std::vector<fs::path>> MSWindowsClipboardFiles::get(HWND window)
{
  // no need to open the clipboard to know there are no files
  if (!IsClipboardFormatAvailable(CF_HDROP))
    return std::nullopt;

  MSWindowsClipboard clipboard(window);
  if (!clipboard.open(0)) {
    LOG_WARN("could not read the copied files: another program has the clipboard open");
    return std::nullopt;
  }
  auto files = readDropFiles(static_cast<HDROP>(GetClipboardData(CF_HDROP)));
  clipboard.close();

  if (files.empty())
    return std::nullopt;
  LOG_DEBUG("%zu files on the clipboard", files.size());
  return files;
}

bool MSWindowsClipboardFiles::set(HWND window, const std::vector<fs::path> &files)
{
  HGLOBAL drop = makeDropFiles(files);
  if (drop == nullptr)
    return false;

  MSWindowsClipboard clipboard(window);
  if (!clipboard.open(0)) {
    GlobalFree(drop);
    return false;
  }

  // empty() marks the clipboard as Deskflow's: not a copy made here
  bool ok = clipboard.empty();
  if (ok && SetClipboardData(CF_HDROP, drop) == nullptr) {
    LOG_WARN("could not put the files on the clipboard: %d", GetLastError());
    ok = false;
  }
  if (!ok) {
    GlobalFree(drop);
  } else if (HGLOBAL effect = makeDropEffect(kDropEffectCopy); effect != nullptr) {
    // pasting copies: a "cut" would move the files out of the cache
    if (SetClipboardData(dropEffectFormat(), effect) == nullptr)
      GlobalFree(effect);
  }
  clipboard.close();
  return ok;
}
