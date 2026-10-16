/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

class LogLevel
{
  //! Log levels
  /*!
  The logging priority levels in order of highest to lowest priority.
  */
public:
  enum class Level
  {
    Print = -1, //!< For print only (no file or time)
    Fatal,      //!< For fatal errors
    Error,      //!< For serious errors
    Warning,    //!< For minor errors and warnings
    Info,       //!< For informational messages
    Debug,      //!< For important debugging messages
    Verbose     //!< For verbose debugging messages
  };

  //! Name of a level as written in settings and log lines ("INFO"); empty if out of range
  static std::string toOption(const LogLevel::Level &level)
  {
    return toOption(static_cast<int>(level));
  }

  static std::string toOption(const int &level)
  {
    if (level < 0 || level >= static_cast<int>(kOptions.size()))
      return {};
    return std::string(kOptions.at(level));
  }

  //! Level for a name, ignoring case; Info if the name is unknown
  static LogLevel::Level fromOption(std::string_view option)
  {
    for (size_t i = 0; i < kOptions.size(); ++i) {
      if (equalsIgnoreCase(kOptions[i], option))
        return LogLevel::Level(static_cast<int>(i));
    }
    return LogLevel::Level::Info;
  }

  static bool isOption(std::string_view option)
  {
    return std::any_of(kOptions.begin(), kOptions.end(), [option](auto o) { return equalsIgnoreCase(o, option); });
  }

  static std::vector<std::string> logLevelOptions()
  {
    return {kOptions.begin(), kOptions.end()};
  }

private:
  static bool equalsIgnoreCase(std::string_view a, std::string_view b)
  {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
             return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
  }

  inline static constexpr std::array<std::string_view, 6> kOptions{"FATAL",  "ERROR", "WARNING",
                                                                   "INFO",   "DEBUG", "VERBOSE"};
};
