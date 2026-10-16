/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <array>
#include <string_view>
#include <vector>

struct CoreOption
{
  std::vector<std::string_view> names; // without dashes; one letter means -x, longer --name
  std::string_view description;
  std::string_view valueName = {}; // set when the option takes a value

  bool takesValue() const
  {
    return !valueName.empty();
  }
};

struct CoreArgs
{
  inline static const CoreOption helpOption{{"h", "help"}, "Display Help on the command line"};
  inline static const CoreOption versionOption{{"v", "version"}, "Display version information"};
  inline static const CoreOption multiInstanceOption{
      {"new-instance"}, "Skip the check for a running instance, always makes a new instance"
  };
  inline static const CoreOption configOption{{"s", "settings"}, "override configuration file to use", "configFile"};

  inline static const std::array<const CoreOption *, 4> options = {
      &helpOption, &versionOption, &multiInstanceOption, &configOption
  };
};
