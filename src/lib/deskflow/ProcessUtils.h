/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <string>
#include <vector>

namespace deskflow {

//! The program's arguments as UTF-8, program name first
/*!
On Windows they come from GetCommandLineW, since argv is in the ANSI code page and
would mangle paths such as a settings file under a non-ASCII user name.
*/
std::vector<std::string> commandLineArguments(int argc, char **argv);

//! The file name of a program path ("deskflow-core.exe")
std::string programName(const std::string &path);

//! Holds a system-wide (per session on Windows) token while it exists
/*!
The first instance to create one with a given name owns it; later ones see
isFirst() == false until the owner exits, even if it crashes. Replaces the
QSharedMemory segment the core used to detect another running core.
*/
class SingleInstance
{
public:
  explicit SingleInstance(const std::string &name);
  ~SingleInstance();

  SingleInstance(const SingleInstance &) = delete;
  SingleInstance &operator=(const SingleInstance &) = delete;

  bool isFirst() const
  {
    return m_first;
  }

private:
  bool m_first = true;
#if defined(_WIN32)
  void *m_handle = nullptr;
#else
  int m_fd = -1;
#endif
};

} // namespace deskflow
