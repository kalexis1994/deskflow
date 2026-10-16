/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace deskflow {

//! A setting's value, converted the way QVariant converts the values QSettings reads
/*!
Values read from a file are strings or string lists, as QSettings returns them; the
typed constructors are for defaults and for writing.
*/
class SettingValue
{
public:
  SettingValue() = default;
  SettingValue(bool value);
  SettingValue(int value);
  SettingValue(long long value);
  SettingValue(double value);
  SettingValue(std::string value);
  SettingValue(std::string_view value);
  SettingValue(const char *value);
  SettingValue(std::vector<std::string> value);

  bool isValid() const
  {
    return m_type != Type::Invalid;
  }

  bool toBool() const;
  int toInt() const;
  unsigned toUInt() const;
  long long toLongLong() const;
  double toDouble() const;
  std::string toString() const;
  std::vector<std::string> toStringList() const;

  //! Parses a raw value from an INI file (everything after `=`)
  static SettingValue fromIni(std::string_view raw);

  //! Encodes the value as QSettings writes it
  std::string toIni() const;

  bool operator==(const SettingValue &other) const = default;

private:
  enum class Type
  {
    Invalid,
    Bool,
    Integer,
    Double,
    String,
    List
  };

  Type m_type = Type::Invalid;
  bool m_bool = false;
  long long m_integer = 0;
  double m_double = 0;
  std::string m_string;
  std::vector<std::string> m_list;
};

//! An INI file in the format QSettings::IniFormat reads and writes
/*!
Keys are full paths ("core/port"): the first part is the section, the rest is
written with backslashes (`screens\1\name`), and keys outside any section live in
`[General]`. Changes edit the text in place, so everything else in the file (order,
blank lines, values this class doesn't understand) is kept as it was.

Keys compare case-insensitively on Windows and macOS, as QSettings does there.
*/
class IniFile
{
public:
  IniFile() = default;
  explicit IniFile(std::string_view text);

  //! Reads a file; a missing or unreadable file gives an empty one
  static IniFile load(const std::filesystem::path &path);

  //! Writes the file through a temporary file, so readers never see half of it
  bool save(const std::filesystem::path &path) const;

  bool contains(std::string_view key) const;
  SettingValue value(std::string_view key, const SettingValue &defaultValue = {}) const;

  //! Sets a key; an invalid value is written as `@Invalid()`, as QSettings does
  void setValue(std::string_view key, const SettingValue &value);

  //! Removes a key and every key below it ("group" removes "group/...")
  void remove(std::string_view key);

  //! Every key, in file order
  std::vector<std::string> keys() const;

  //! The top-level groups ("core", "computer_x", ...), in file order
  std::vector<std::string> childGroups() const;

  std::string text() const;

private:
  struct Line
  {
    std::string lead; // blank lines and comments before the line, with the previous line break
    std::string body; // the line itself, without its line break
    std::string section;
    std::string key; // full key, empty for section headers and other lines
    bool isSection = false;
  };

  void parse(std::string_view text);
  int find(std::string_view key) const;
  std::string eol() const;

  std::vector<Line> m_lines;
  std::string m_tail;
  bool m_crlf =
#if defined(_WIN32)
      true;
#else
      false;
#endif
};

} // namespace deskflow
