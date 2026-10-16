/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <map>
#include <string>
#include <vector>

struct CoreOption;

//! The core's command line: `deskflow-core server|client [options]`
/*!
Accepts what QCommandLineParser accepted before: `-s file`, `--settings file`,
`--settings=file`, single-dash long names (`-settings`), options before or after the
mode, and `--` to end the options.
*/
class CoreArgParser
{
public:
  /**
   * @brief CoreArgParser parses the arguments (program name first) and sets the help and version options
   * For any other settings to be set you must call parse();
   * @sa parse();
   */
  explicit CoreArgParser(const std::vector<std::string> &args = {});
  /**
   * @brief parse
   * This method will parse all options other then help and version
   */
  void parse();
  std::string helpText() const;
  std::string versionText() const;
  std::string errorText() const;
  bool help() const;
  bool version() const;
  bool serverMode() const;
  bool clientMode() const;
  bool singleInstanceOnly() const;

private:
  [[noreturn]] void showHelpText() const;
  bool isSet(const CoreOption &option) const;
  std::string value(const CoreOption &option) const;

  std::map<const CoreOption *, std::string> m_set;
  std::vector<std::string> m_positional;
  std::string m_error;
  bool m_clientMode = false;
  bool m_serverMode = false;
};
