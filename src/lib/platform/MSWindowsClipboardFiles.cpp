/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFiles.h"

#include "base/Log.h"
#include "deskflow/files/FileTransfer.h"
#include "platform/MSWindowsClipboard.h"

#include <ShlObj.h>

#include <array>
#include <cstring>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

constexpr DWORD kDropEffectCopy = 1; // DROPEFFECT_COPY
constexpr DWORD kHelperTimeoutMs = 5000;
constexpr int kOpenAttempts = 10;
constexpr DWORD kOpenRetryMs = 20;

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

std::string MSWindowsClipboardFiles::packPaths(const std::vector<fs::path> &files)
{
  std::string packed;
  for (const auto &file : files) {
    const auto &path = file.native();
    packed.append(reinterpret_cast<const char *>(path.c_str()), (path.size() + 1) * sizeof(wchar_t));
  }
  return packed;
}

std::vector<fs::path> MSWindowsClipboardFiles::unpackPaths(const std::string &packed)
{
  std::wstring text(packed.size() / sizeof(wchar_t), L'\0');
  std::memcpy(text.data(), packed.data(), text.size() * sizeof(wchar_t));

  std::vector<fs::path> files;
  std::size_t start = 0;
  // a path cut short has no terminator: the output was truncated, so it's not a path
  for (auto end = text.find(L'\0', start); end != std::wstring::npos; end = text.find(L'\0', start)) {
    if (end > start)
      files.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  return files;
}

int MSWindowsClipboardFiles::printFiles()
{
  // this runs before the core sets anything up, logging included
  bool opened = false;
  for (int i = 0; i < kOpenAttempts && !opened; ++i) {
    opened = OpenClipboard(nullptr) != FALSE;
    if (!opened)
      Sleep(kOpenRetryMs);
  }
  if (!opened)
    return 1;
  const auto files = readDropFiles(static_cast<HDROP>(GetClipboardData(CF_HDROP)));
  CloseClipboard();

  const auto packed = packPaths(files);
  const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
  std::size_t sent = 0;
  while (sent < packed.size()) {
    DWORD written = 0;
    if (!WriteFile(output, packed.data() + sent, static_cast<DWORD>(packed.size() - sent), &written, nullptr))
      return 1;
    sent += written;
  }
  return 0;
}

std::optional<std::vector<fs::path>> MSWindowsClipboardFiles::readThroughHelper(const fs::path &program, HANDLE token)
{
  SECURITY_ATTRIBUTES inheritable = {sizeof(inheritable), nullptr, TRUE};
  HANDLE readEnd = nullptr;
  HANDLE writeEnd = nullptr;
  if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
    return std::nullopt;
  SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

  // the pipe and nothing else: this process may be SYSTEM, and the helper is the user
  SIZE_T attributesSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
  std::vector<std::byte> attributesBytes(attributesSize);
  auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesBytes.data());
  bool ready = InitializeProcThreadAttributeList(attributes, 1, 0, &attributesSize) != FALSE;
  ready = ready && UpdateProcThreadAttribute(
                       attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &writeEnd, sizeof(writeEnd), nullptr, nullptr
                   ) != FALSE;

  wchar_t desktop[] = L"winsta0\\default";
  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdOutput = writeEnd;
  startup.StartupInfo.lpDesktop = desktop;
  startup.lpAttributeList = attributes;

  auto command = L"\"" + program.native() + L"\" " + fs::path(kPrintFilesArg).native();
  const DWORD flags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
  PROCESS_INFORMATION process = {};
  BOOL started = FALSE;
  if (ready && token != nullptr) {
    started = CreateProcessAsUserW(
        token, program.c_str(), command.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &startup.StartupInfo,
        &process
    );
  } else if (ready) {
    started = CreateProcessW(
        program.c_str(), command.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &startup.StartupInfo, &process
    );
  }
  const auto startError = GetLastError();
  if (ready)
    DeleteProcThreadAttributeList(attributes);
  CloseHandle(writeEnd); // or the reader would never see the end of the output
  if (!started) {
    LOG_WARN("could not start the helper that reads the copied files: %d", startError);
    CloseHandle(readEnd);
    return std::nullopt;
  }

  std::string output;
  std::thread reader([&output, readEnd] {
    std::array<char, 4096> buffer;
    DWORD got = 0;
    while (ReadFile(readEnd, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) && got > 0)
      output.append(buffer.data(), got);
  });

  // Explorer renders the list when asked for it, and a hung program must not stall this
  const bool finished = WaitForSingleObject(process.hProcess, kHelperTimeoutMs) == WAIT_OBJECT_0;
  if (!finished) {
    LOG_WARN("the helper that reads the copied files didn't answer");
    TerminateProcess(process.hProcess, 1);
  }
  reader.join();

  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(readEnd);
  if (!finished || exitCode != 0)
    return std::nullopt;
  return unpackPaths(output);
}

std::optional<std::vector<fs::path>> MSWindowsClipboardFiles::get(HWND window)
{
  // SYSTEM, in the user's session, sees only the first format of what the user copied
  // (docs/dev/file-clipboard.md), so the list is read by a process of that user.
  if (deskflow::files::runningAsSystem()) {
    const auto token = static_cast<HANDLE>(deskflow::files::sessionUserToken());
    if (token == nullptr)
      return std::nullopt; // a login screen: nobody to have copied anything

    std::wstring program(32768, L'\0');
    program.resize(GetModuleFileNameW(nullptr, program.data(), static_cast<DWORD>(program.size())));
    const auto files = readThroughHelper(program, token);
    CloseHandle(token);

    // temporary, to learn why SYSTEM can't see them
    const bool seen = IsClipboardFormatAvailable(CF_HDROP) != FALSE;
    bool seenAsUser = false;
    {
      deskflow::files::AsSessionUser asUser;
      seenAsUser = asUser.ok() && IsClipboardFormatAvailable(CF_HDROP) != FALSE;
    }
    LOG_INFO(
        "clipboard files: %zu through the helper; files visible here=%d, impersonating the user=%d",
        files ? files->size() : 0, seen ? 1 : 0, seenAsUser ? 1 : 0
    );

    if (!files || files->empty())
      return std::nullopt;
    return files;
  }

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
