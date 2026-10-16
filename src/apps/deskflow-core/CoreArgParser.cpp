/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreArgParser.h"
#include "CoreArgs.h"

#include "common/Constants.h"
#include "common/CoreSettings.h"
#include "common/ExitCodes.h"
#include "common/VersionInfo.h"
#include "deskflow/ProtocolTypes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

const CoreOption *findOption(std::string_view name)
{
  for (const auto *option : CoreArgs::options) {
    if (std::find(option->names.begin(), option->names.end(), name) != option->names.end())
      return option;
  }
  return nullptr;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

//! "  -s, --settings <configFile>", the left column of the help text
std::string optionSyntax(const CoreOption &option)
{
  std::string syntax = "  ";
  for (size_t i = 0; i < option.names.size(); ++i) {
    if (i != 0)
      syntax += ", ";
    syntax += option.names[i].size() == 1 ? "-" : "--";
    syntax += option.names[i];
  }
  if (option.takesValue()) {
    syntax += " <";
    syntax += option.valueName;
    syntax += ">";
  }
  return syntax;
}

//! Word-wraps a description into the right column, as QCommandLineParser laid it out
void appendRow(std::string &text, const std::string &left, std::string_view description, size_t column)
{
  constexpr size_t kLineWidth = 79;
  text += left;
  text.append(column - left.size(), ' ');

  size_t lineLength = column;
  bool firstWord = true;
  size_t start = 0;
  while (start < description.size()) {
    auto end = description.find(' ', start);
    if (end == std::string_view::npos)
      end = description.size();
    const auto word = description.substr(start, end - start);
    if (!firstWord && lineLength + 1 + word.size() > kLineWidth) {
      text += '\n';
      text.append(column, ' ');
      lineLength = column;
      firstWord = true;
    }
    if (!firstWord) {
      text += ' ';
      ++lineLength;
    }
    text += word;
    lineLength += word.size();
    firstWord = false;
    start = end + 1;
  }
  text += '\n';
}

} // namespace

CoreArgParser::CoreArgParser(const std::vector<std::string> &args)
{
  bool optionsEnded = false;
  for (size_t i = 1; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (optionsEnded || arg.size() < 2 || arg[0] != '-') {
      m_positional.push_back(arg);
      continue;
    }
    if (arg == "--") {
      optionsEnded = true;
      continue;
    }

    // one or two dashes: single-dash words are long names too ("-settings")
    auto name = std::string_view(arg).substr(arg[1] == '-' ? 2 : 1);
    std::string inlineValue;
    bool hasInlineValue = false;
    if (const auto equals = name.find('='); equals != std::string_view::npos) {
      inlineValue = std::string(name.substr(equals + 1));
      hasInlineValue = true;
      name = name.substr(0, equals);
    }

    const auto *option = findOption(name);
    if (!option) {
      if (m_error.empty())
        m_error = "Unknown option '" + std::string(name) + "'.";
      continue;
    }

    if (!option->takesValue()) {
      if (hasInlineValue && m_error.empty())
        m_error = "Unexpected value after '" + arg.substr(0, arg.find('=')) + "'.";
      m_set[option] = {};
      continue;
    }

    if (hasInlineValue) {
      m_set[option] = inlineValue;
    } else if (i + 1 < args.size()) {
      m_set[option] = args[++i];
    } else if (m_error.empty()) {
      m_error = "Missing value after '" + arg + "'.";
    }
  }
}

void CoreArgParser::parse()
{
  if (m_positional.empty()) {
    showHelpText();
  }

  const auto &mode = m_positional.front();
  m_serverMode = equalsIgnoreCase(mode, "server");
  m_clientMode = equalsIgnoreCase(mode, "client");

  if ((!m_clientMode && !m_serverMode) || mode.empty()) {
    showHelpText();
  }

  if (isSet(CoreArgs::configOption)) {
    CoreSettings::setSettingsFile(value(CoreArgs::configOption));
  }
}

[[noreturn]] void CoreArgParser::showHelpText() const
{
  std::fputs(helpText().c_str(), stdout);
  std::fflush(stdout);
  exit(s_exitSuccess);
}

std::string CoreArgParser::helpText() const
{
  std::string text = std::string(kCoreBinName) + ": " + kDisplayVersion + "\n";
  text += std::string("Usage: ") + kCoreBinName + " coremode [options]\n";
  text += std::string(kAppDescription) + "\n\nOptions:\n";

  size_t column = std::string("  coremode").size();
  for (const auto *option : CoreArgs::options)
    column = std::max(column, optionSyntax(*option).size());
  column += 2;

  for (const auto *option : CoreArgs::options)
    appendRow(text, optionSyntax(*option), option->description, column);

  text += "\nArguments:\n";
  appendRow(text, "  coremode", "The mode to start in either: server or client", column);
  return text;
}

std::string CoreArgParser::versionText() const
{
  return std::string(kCoreBinName) + " v" + kDisplayVersion + ", protocol v" + std::to_string(kProtocolMajorVersion) +
         "." + std::to_string(kProtocolMinorVersion) + "\n" + kCopyright + "\n";
}

std::string CoreArgParser::errorText() const
{
  return m_error;
}

bool CoreArgParser::help() const
{
  return isSet(CoreArgs::helpOption);
}

bool CoreArgParser::version() const
{
  return isSet(CoreArgs::versionOption);
}

bool CoreArgParser::serverMode() const
{
  return m_serverMode;
}

bool CoreArgParser::clientMode() const
{
  return m_clientMode;
}

bool CoreArgParser::singleInstanceOnly() const
{
  return !isSet(CoreArgs::multiInstanceOption);
}

bool CoreArgParser::isSet(const CoreOption &option) const
{
  return m_set.contains(&option);
}

std::string CoreArgParser::value(const CoreOption &option) const
{
  const auto it = m_set.find(&option);
  return it == m_set.end() ? std::string() : it->second;
}
