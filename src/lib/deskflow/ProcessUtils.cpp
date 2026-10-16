/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ProcessUtils.h"
#include "arch/OsDetection.h"

#include "arch/Utf8.h"
#include "base/Log.h"

#if defined(DESKFLOW_OS_WIN)
#include <shellapi.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace deskflow {

std::vector<std::string> commandLineArguments(int argc, char **argv)
{
  std::vector<std::string> args;
#if defined(DESKFLOW_OS_WIN)
  int count = 0;
  if (LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &count); wide) {
    for (int i = 0; i < count; ++i)
      args.push_back(wideToUtf8(wide[i]));
    LocalFree(wide);
    return args;
  }
#endif
  for (int i = 0; i < argc; ++i)
    args.emplace_back(argv[i]);
  return args;
}

std::string programName(const std::string &path)
{
  return pathToUtf8(utf8Path(path).filename());
}

#if defined(DESKFLOW_OS_WIN)

SingleInstance::SingleInstance(const std::string &name)
{
  // a named mutex in this session's namespace, as QSharedMemory's segment was;
  // Windows deletes it when the last handle closes, so a crash never leaves it behind
  const auto wideName = utf8ToWide("Local\\" + name + "-instance");
  m_handle = CreateMutexW(nullptr, FALSE, wideName.c_str());
  m_first = m_handle == nullptr || GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance()
{
  if (m_handle)
    CloseHandle(static_cast<HANDLE>(m_handle));
}

#else

SingleInstance::SingleInstance(const std::string &name)
{
  // a lock on a file in the temp directory; the system drops it when the process exits
  std::string temp;
  if (const char *tmpdir = std::getenv("TMPDIR"); tmpdir && *tmpdir)
    temp = tmpdir;
  if (temp.empty())
    temp = "/tmp";
  const auto path = temp + "/" + name + ".lock";

  m_fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  if (m_fd < 0) {
    LOG_DEBUG("could not open instance lock %s: %s", path.c_str(), std::strerror(errno));
    return; // can't tell, so don't stop this instance
  }
  m_first = ::flock(m_fd, LOCK_EX | LOCK_NB) == 0;
}

SingleInstance::~SingleInstance()
{
  if (m_fd >= 0)
    ::close(m_fd);
}

#endif

} // namespace deskflow
