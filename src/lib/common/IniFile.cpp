/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/IniFile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

// The parsing and escaping below follow QSettings (qsettings.cpp) closely, so files
// written by either side read the same in the other.

namespace deskflow {

namespace {

#if defined(_WIN32) || defined(__APPLE__)
constexpr bool kKeysIgnoreCase = true;
#else
constexpr bool kKeysIgnoreCase = false;
#endif

char asciiLower(char c)
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return asciiLower(x) == asciiLower(y); });
}

bool keyEquals(std::string_view a, std::string_view b)
{
  return kKeysIgnoreCase ? equalsIgnoreCase(a, b) : a == b;
}

bool keyStartsWith(std::string_view key, std::string_view prefix)
{
  return key.size() >= prefix.size() && keyEquals(key.substr(0, prefix.size()), prefix);
}

std::string_view trimmed(std::string_view s)
{
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  while (!s.empty() && isSpace(s.front()))
    s.remove_prefix(1);
  while (!s.empty() && isSpace(s.back()))
    s.remove_suffix(1);
  return s;
}

int fromHex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

int fromOct(char c)
{
  return (c >= '0' && c <= '7') ? c - '0' : -1;
}

void appendUtf8(std::string &out, char32_t cp)
{
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// QSettingsPrivate::iniEscapedKey: '/' becomes '\', anything but letters, digits
// and "_-." becomes %XX (or %UXXXX past Latin-1).
std::string escapeKey(std::string_view key)
{
  static const char hex[] = "0123456789ABCDEF";
  std::string result;
  for (size_t i = 0; i < key.size(); ++i) {
    const auto ch = static_cast<unsigned char>(key[i]);
    if (ch == '/') {
      result += '\\';
    } else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
               ch == '-' || ch == '.') {
      result += static_cast<char>(ch);
    } else {
      // decode one UTF-8 sequence to get the code point Qt escapes
      char32_t cp = ch;
      size_t extra = 0;
      if (ch >= 0xF0)
        cp = ch & 0x07, extra = 3;
      else if (ch >= 0xE0)
        cp = ch & 0x0F, extra = 2;
      else if (ch >= 0xC0)
        cp = ch & 0x1F, extra = 1;
      for (size_t j = 0; j < extra && i + 1 < key.size(); ++j)
        cp = (cp << 6) | (static_cast<unsigned char>(key[++i]) & 0x3F);
      if (cp <= 0xFF) {
        result += '%';
        result += hex[cp / 16];
        result += hex[cp % 16];
      } else {
        result += "%U";
        for (int shift = 12; shift >= 0; shift -= 4)
          result += hex[(cp >> shift) & 0xF];
      }
    }
  }
  return result;
}

// QSettingsPrivate::iniUnescapedKey
std::string unescapeKey(std::string_view key)
{
  std::string result;
  size_t i = 0;
  while (i < key.size()) {
    const char ch = key[i];
    if (ch == '\\') {
      result += '/';
      ++i;
      continue;
    }
    if (ch != '%' || i == key.size() - 1) {
      result += ch;
      ++i;
      continue;
    }

    size_t numDigits = 2;
    size_t firstDigitPos = i + 1;
    if (key[i + 1] == 'U') {
      ++firstDigitPos;
      numDigits = 4;
    }
    if (firstDigitPos + numDigits > key.size()) {
      result += '%';
      ++i;
      continue;
    }

    char32_t cp = 0;
    bool ok = true;
    for (size_t j = 0; j < numDigits; ++j) {
      const int digit = fromHex(key[firstDigitPos + j]);
      if (digit < 0) {
        ok = false;
        break;
      }
      cp = (cp << 4) | static_cast<char32_t>(digit);
    }
    if (!ok) {
      result += '%';
      ++i;
      continue;
    }
    appendUtf8(result, cp);
    i = firstDigitPos + numDigits;
  }
  return result;
}

// QSettingsPrivate::iniEscapedString, for UTF-8 text
void appendEscapedString(std::string &result, std::string_view str)
{
  bool needsQuotes = false;
  bool escapeNextIfDigit = false;
  const size_t startPos = result.size();

  for (const char c : str) {
    const auto ch = static_cast<unsigned char>(c);
    if (ch == ';' || ch == ',' || ch == '=')
      needsQuotes = true;

    if (escapeNextIfDigit && fromHex(c) != -1) {
      std::array<char, 8> digits{};
      const auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), ch, 16);
      result += "\\x";
      result.append(digits.data(), end);
      continue;
    }
    escapeNextIfDigit = false;

    switch (ch) {
    case '\0':
      result += "\\0";
      escapeNextIfDigit = true;
      break;
    case '\a':
      result += "\\a";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    case '\v':
      result += "\\v";
      break;
    case '"':
    case '\\':
      result += '\\';
      result += c;
      break;
    default:
      if (ch <= 0x1F) {
        std::array<char, 8> digits{};
        const auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), ch, 16);
        result += "\\x";
        result.append(digits.data(), end);
        escapeNextIfDigit = true;
      } else {
        result += c;
      }
    }
  }

  if (needsQuotes || (startPos < result.size() && (result[startPos] == ' ' || result.back() == ' '))) {
    result.insert(startPos, 1, '"');
    result += '"';
  }
}

void chopTrailingSpaces(std::string &str, size_t limit)
{
  while (str.size() > limit && (str.back() == ' ' || str.back() == '\t'))
    str.pop_back();
}

// QSettingsPrivate::iniUnescapedStringList; returns true when the value is a list
bool unescapeStringList(std::string_view str, std::string &stringResult, std::vector<std::string> &listResult)
{
  static constexpr std::array<std::pair<char, char>, 11> escapeCodes{
      {{'a', '\a'},
       {'b', '\b'},
       {'f', '\f'},
       {'n', '\n'},
       {'r', '\r'},
       {'t', '\t'},
       {'v', '\v'},
       {'"', '"'},
       {'?', '?'},
       {'\'', '\''},
       {'\\', '\\'}}
  };

  bool isStringList = false;
  bool inQuotedString = false;
  bool currentValueIsQuoted = false;
  size_t i = 0;

  const auto skipSpaces = [&] {
    while (i < str.size() && (str[i] == ' ' || str[i] == '\t'))
      ++i;
  };
  // reads \x... or \0... digits into a code point, Qt keeps it as one UTF-16 unit
  const auto readNumericEscape = [&](char32_t value, int base) {
    while (i < str.size()) {
      const int digit = base == 16 ? fromHex(str[i]) : fromOct(str[i]);
      if (digit < 0)
        break;
      value = ((value * base) + static_cast<char32_t>(digit)) & 0xFFFF;
      ++i;
    }
    appendUtf8(stringResult, value);
  };

  skipSpaces();
  size_t chopLimit = stringResult.size();
  while (i < str.size()) {
    switch (str[i]) {
    case '\\': {
      ++i;
      if (i >= str.size())
        goto end;
      const char ch = str[i++];
      const auto code =
          std::find_if(escapeCodes.begin(), escapeCodes.end(), [ch](const auto &e) { return e.first == ch; });
      if (code != escapeCodes.end()) {
        stringResult += code->second;
        chopLimit = stringResult.size();
        continue;
      }
      if (ch == 'x') {
        if (i >= str.size())
          goto end;
        if (fromHex(str[i]) != -1)
          readNumericEscape(0, 16);
      } else if (const int o = fromOct(ch); o != -1) {
        readNumericEscape(static_cast<char32_t>(o), 8);
      } else if (ch == '\n' || ch == '\r') {
        if (i < str.size()) {
          const char ch2 = str[i];
          // \n, \r, \r\n, and \n\r are legitimate line terminators in INI files
          if ((ch2 == '\n' || ch2 == '\r') && ch2 != ch)
            ++i;
        }
      }
      // any other escaped character is skipped
      chopLimit = stringResult.size();
      break;
    }
    case '"':
      ++i;
      currentValueIsQuoted = true;
      inQuotedString = !inQuotedString;
      if (!inQuotedString) {
        skipSpaces();
        chopLimit = stringResult.size();
      }
      break;
    case ',':
      if (!inQuotedString) {
        if (!currentValueIsQuoted)
          chopTrailingSpaces(stringResult, chopLimit);
        if (!isStringList) {
          isStringList = true;
          listResult.clear();
        }
        listResult.push_back(stringResult);
        stringResult.clear();
        currentValueIsQuoted = false;
        ++i;
        skipSpaces();
        chopLimit = stringResult.size();
        break;
      }
      [[fallthrough]];
    default: {
      size_t j = i + 1;
      while (j < str.size() && str[j] != '\\' && str[j] != '"' && str[j] != ',')
        ++j;
      stringResult.append(str.substr(i, j - i));
      i = j;
    }
    }
  }
  if (!currentValueIsQuoted)
    chopTrailingSpaces(stringResult, chopLimit);

end:
  if (isStringList)
    listResult.push_back(stringResult);
  return isStringList;
}

// QSettingsPrivate::stringToVariant, for the types settings use
SettingValue stringToValue(const std::string &s)
{
  if (s.starts_with('@')) {
    if (s.ends_with(')')) {
      if (s.starts_with("@ByteArray("))
        return SettingValue(s.substr(11, s.size() - 12));
      if (s.starts_with("@String("))
        return SettingValue(s.substr(8, s.size() - 9));
      if (s == "@Invalid()")
        return {};
      if (s.starts_with("@Variant(") || s.starts_with("@DateTime(") || s.starts_with("@Rect(") ||
          s.starts_with("@Size(") || s.starts_with("@Point("))
        return {}; // binary or geometry values; nothing outside the old GUI reads these
    }
    if (s.starts_with("@@"))
      return SettingValue(s.substr(1));
  }
  return SettingValue(s);
}

std::string atEscaped(const std::string &s)
{
  return s.starts_with('@') ? "@" + s : s;
}

std::string doubleToString(double value)
{
  std::array<char, 32> buffer{};
  const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), end);
}

template <typename T> bool parseNumber(std::string_view text, T &out)
{
  text = trimmed(text);
  if (text.starts_with('+'))
    text.remove_prefix(1);
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc() && ptr == text.data() + text.size();
}

// QConfFileSettingsPrivate::readIniLine
bool readIniLine(std::string_view data, size_t &dataPos, size_t &lineStart, size_t &lineLen, size_t &equalsPos)
{
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  const auto isSpecial = [](char c) { return c == '\n' || c == '\r' || c == '"' || c == ';' || c == '=' || c == '\\'; };

  const size_t dataLen = data.size();
  bool inQuotes = false;
  equalsPos = std::string_view::npos;

  lineStart = dataPos;
  while (lineStart < dataLen && isSpace(data[lineStart]))
    ++lineStart;

  size_t i = lineStart;
  while (i < dataLen) {
    char ch = data[i];
    while (!isSpecial(ch)) {
      if (++i == dataLen)
        goto done;
      ch = data[i];
    }

    ++i;
    if (ch == '=') {
      if (!inQuotes && equalsPos == std::string_view::npos)
        equalsPos = i - 1;
    } else if (ch == '\n' || ch == '\r') {
      if (i == lineStart + 1) {
        ++lineStart;
      } else if (!inQuotes) {
        --i;
        goto done;
      }
    } else if (ch == '\\') {
      if (i < dataLen) {
        const char next = data[i++];
        if (i < dataLen) {
          const char next2 = data[i];
          // \n, \r, \r\n, and \n\r are legitimate line terminators in INI files
          if ((next == '\n' && next2 == '\r') || (next == '\r' && next2 == '\n'))
            ++i;
        }
      }
    } else if (ch == '"') {
      inQuotes = !inQuotes;
    } else { // ';'
      if (i == lineStart + 1) {
        while (i < dataLen && data[i] != '\n' && data[i] != '\r')
          ++i;
        while (i < dataLen && isSpace(data[i]))
          ++i;
        lineStart = i;
      } else if (!inQuotes) {
        --i;
        goto done;
      }
    }
  }

done:
  dataPos = i;
  lineLen = i - lineStart;
  return lineLen > 0;
}

std::string sectionOf(std::string_view key)
{
  const auto slash = key.find('/');
  return slash == std::string_view::npos ? std::string() : std::string(key.substr(0, slash));
}

std::string sectionHeader(const std::string &section)
{
  if (section.empty())
    return "[General]";
  if (equalsIgnoreCase(section, "general"))
    return "[%General]";
  return "[" + escapeKey(section) + "]";
}

} // namespace

//
// SettingValue
//

SettingValue::SettingValue(bool value) : m_type(Type::Bool), m_bool(value)
{
}

SettingValue::SettingValue(int value) : m_type(Type::Integer), m_integer(value)
{
}

SettingValue::SettingValue(long long value) : m_type(Type::Integer), m_integer(value)
{
}

SettingValue::SettingValue(double value) : m_type(Type::Double), m_double(value)
{
}

SettingValue::SettingValue(std::string value) : m_type(Type::String), m_string(std::move(value))
{
}

SettingValue::SettingValue(std::string_view value) : m_type(Type::String), m_string(value)
{
}

SettingValue::SettingValue(const char *value) : m_type(Type::String), m_string(value ? value : "")
{
}

SettingValue::SettingValue(std::vector<std::string> value) : m_type(Type::List), m_list(std::move(value))
{
}

bool SettingValue::toBool() const
{
  switch (m_type) {
  case Type::Bool:
    return m_bool;
  case Type::Integer:
    return m_integer != 0;
  case Type::Double:
    return m_double != 0;
  case Type::String:
    return !(m_string.empty() || m_string == "0" || equalsIgnoreCase(m_string, "false"));
  default:
    return false;
  }
}

long long SettingValue::toLongLong() const
{
  switch (m_type) {
  case Type::Bool:
    return m_bool ? 1 : 0;
  case Type::Integer:
    return m_integer;
  case Type::Double:
    return std::llround(m_double);
  case Type::String: {
    long long result = 0;
    return parseNumber(m_string, result) ? result : 0;
  }
  default:
    return 0;
  }
}

int SettingValue::toInt() const
{
  return static_cast<int>(toLongLong());
}

unsigned SettingValue::toUInt() const
{
  if (m_type == Type::String) {
    unsigned result = 0;
    return parseNumber(m_string, result) ? result : 0;
  }
  return static_cast<unsigned>(toLongLong());
}

double SettingValue::toDouble() const
{
  switch (m_type) {
  case Type::Bool:
    return m_bool ? 1 : 0;
  case Type::Integer:
    return static_cast<double>(m_integer);
  case Type::Double:
    return m_double;
  case Type::String: {
    double result = 0;
    return parseNumber(m_string, result) ? result : 0;
  }
  default:
    return 0;
  }
}

std::string SettingValue::toString() const
{
  switch (m_type) {
  case Type::Bool:
    return m_bool ? "true" : "false";
  case Type::Integer:
    return std::to_string(m_integer);
  case Type::Double:
    return doubleToString(m_double);
  case Type::String:
    return m_string;
  case Type::List:
    return m_list.size() == 1 ? m_list.front() : std::string();
  default:
    return {};
  }
}

std::vector<std::string> SettingValue::toStringList() const
{
  switch (m_type) {
  case Type::List:
    return m_list;
  case Type::Invalid:
    return {};
  default:
    return {toString()};
  }
}

SettingValue SettingValue::fromIni(std::string_view raw)
{
  std::string string;
  std::vector<std::string> list;
  if (!unescapeStringList(raw, string, list))
    return stringToValue(string);

  for (auto &item : list) {
    if (item.starts_with("@@"))
      item.erase(0, 1);
  }
  return SettingValue(std::move(list));
}

std::string SettingValue::toIni() const
{
  std::string result;
  switch (m_type) {
  case Type::Invalid:
    return "@Invalid()";
  case Type::String:
    appendEscapedString(result, atEscaped(m_string));
    return result;
  case Type::List:
    if (m_list.empty())
      return "@Invalid()"; // what QSettings writes for an empty list
    for (size_t i = 0; i < m_list.size(); ++i) {
      if (i != 0)
        result += ", ";
      appendEscapedString(result, atEscaped(m_list[i]));
    }
    return result;
  default:
    return toString();
  }
}

//
// IniFile
//

IniFile::IniFile(std::string_view text)
{
  parse(text);
}

IniFile IniFile::load(const std::filesystem::path &path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  std::stringstream buffer;
  buffer << file.rdbuf();
  return IniFile(buffer.str());
}

bool IniFile::save(const std::filesystem::path &path) const
{
  std::error_code ec;
  if (path.has_parent_path())
    std::filesystem::create_directories(path.parent_path(), ec);

  const auto content = text();
  auto temp = path;
  temp += ".tmp" + std::to_string(std::random_device{}());
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.close();
    if (file) {
      std::filesystem::rename(temp, path, ec);
      if (!ec)
        return true;
    }
  }
  std::filesystem::remove(temp, ec);

  // couldn't replace the file (e.g. no rights to create files next to it): write it directly
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(file);
}

void IniFile::parse(std::string_view data)
{
  m_lines.clear();
  if (data.find("\r\n") != std::string_view::npos)
    m_crlf = true;
  else if (data.find('\n') != std::string_view::npos)
    m_crlf = false;

  std::string section;
  size_t pos = data.starts_with("\xEF\xBB\xBF") ? 3 : 0; // the BOM stays in the first lead
  size_t previousEnd = 0;
  size_t lineStart = 0;
  size_t lineLen = 0;
  size_t equalsPos = 0;

  while (readIniLine(data, pos, lineStart, lineLen, equalsPos)) {
    Line line;
    line.lead = data.substr(previousEnd, lineStart - previousEnd);
    line.body = data.substr(lineStart, lineLen);
    previousEnd = lineStart + lineLen;

    if (data[lineStart] == '[') {
      const auto close = data.find(']', lineStart);
      auto name = (close == std::string_view::npos || close >= lineStart + lineLen)
                      ? data.substr(lineStart + 1, lineLen - 1)
                      : data.substr(lineStart + 1, close - lineStart - 1);
      name = trimmed(name);
      if (equalsIgnoreCase(name, "general"))
        section.clear();
      else if (equalsIgnoreCase(name, "%general"))
        section = name.substr(1);
      else
        section = unescapeKey(name);
      line.isSection = true;
    } else if (equalsPos != std::string_view::npos) {
      auto keyEnd = equalsPos;
      while (keyEnd > lineStart && (data[keyEnd - 1] == ' ' || data[keyEnd - 1] == '\t'))
        --keyEnd;
      const auto key = unescapeKey(data.substr(lineStart, keyEnd - lineStart));
      line.key = section.empty() ? key : section + "/" + key;
    }
    line.section = section;
    m_lines.push_back(std::move(line));
  }
  m_tail = data.substr(previousEnd);
}

int IniFile::find(std::string_view key) const
{
  for (auto i = static_cast<int>(m_lines.size()) - 1; i >= 0; --i) {
    if (!m_lines[i].key.empty() && keyEquals(m_lines[i].key, key))
      return i;
  }
  return -1;
}

std::string IniFile::eol() const
{
  return m_crlf ? "\r\n" : "\n";
}

bool IniFile::contains(std::string_view key) const
{
  return find(key) >= 0;
}

SettingValue IniFile::value(std::string_view key, const SettingValue &defaultValue) const
{
  const auto index = find(key);
  if (index < 0)
    return defaultValue;
  const auto &body = m_lines[index].body;
  return SettingValue::fromIni(std::string_view(body).substr(body.find('=') + 1));
}

void IniFile::setValue(std::string_view key, const SettingValue &value)
{
  if (key.empty())
    return;

  if (const auto index = find(key); index >= 0) {
    auto &body = m_lines[index].body;
    body = body.substr(0, body.find('=') + 1) + value.toIni();
    return;
  }

  const auto section = sectionOf(key);
  const auto subKey = section.empty() ? key : key.substr(section.size() + 1);
  Line line;
  line.lead = eol();
  line.body = escapeKey(subKey) + "=" + value.toIni();
  line.section = section;
  line.key = std::string(key);

  // after the last line of the key's section (for keys outside sections: the last of
  // the lines above the first section, or of [General])
  for (auto i = static_cast<int>(m_lines.size()) - 1; i >= 0; --i) {
    const auto &existing = m_lines[i];
    if (!keyEquals(existing.section, section))
      continue;
    if (!section.empty() || existing.isSection || !existing.key.empty()) {
      m_lines.insert(m_lines.begin() + i + 1, std::move(line));
      return;
    }
  }

  Line header;
  header.isSection = true;
  header.section = section;
  header.body = sectionHeader(section);

  if (section.empty()) {
    // [General] goes first
    header.lead = m_lines.empty() ? std::string() : m_lines.front().lead;
    if (!m_lines.empty())
      m_lines.front().lead = eol() + eol();
    m_lines.insert(m_lines.begin(), {std::move(header), std::move(line)});
  } else {
    header.lead = m_lines.empty() ? std::string() : eol() + eol();
    m_lines.push_back(std::move(header));
    m_lines.push_back(std::move(line));
  }
  if (m_tail.empty())
    m_tail = eol();
}

void IniFile::remove(std::string_view key)
{
  if (key.empty())
    return;

  const auto prefix = std::string(key) + "/";
  std::erase_if(m_lines, [&](const Line &line) {
    return !line.key.empty() && (keyEquals(line.key, key) || keyStartsWith(line.key, prefix));
  });

  // drop section headers left without keys
  for (size_t i = 0; i < m_lines.size();) {
    const bool empty = m_lines[i].isSection && (i + 1 == m_lines.size() || m_lines[i + 1].isSection);
    if (empty)
      m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(i));
    else
      ++i;
  }

  // a removed first line leaves the next one's line break at the top of the file
  if (!m_lines.empty()) {
    auto &lead = m_lines.front().lead;
    const auto bom = lead.starts_with("\xEF\xBB\xBF") ? std::string("\xEF\xBB\xBF") : std::string();
    auto rest = std::string_view(lead).substr(bom.size());
    while (!rest.empty() && (rest.front() == '\r' || rest.front() == '\n'))
      rest.remove_prefix(1);
    lead = bom + std::string(rest);
  }
}

std::vector<std::string> IniFile::keys() const
{
  std::vector<std::string> result;
  for (const auto &line : m_lines) {
    if (line.key.empty())
      continue;
    if (std::none_of(result.begin(), result.end(), [&](const auto &k) { return keyEquals(k, line.key); }))
      result.push_back(line.key);
  }
  return result;
}

std::vector<std::string> IniFile::childGroups() const
{
  std::vector<std::string> result;
  for (const auto &key : keys()) {
    const auto group = sectionOf(key);
    if (group.empty())
      continue;
    if (std::none_of(result.begin(), result.end(), [&](const auto &g) { return keyEquals(g, group); }))
      result.push_back(group);
  }
  return result;
}

std::string IniFile::text() const
{
  std::string result;
  for (const auto &line : m_lines) {
    result += line.lead;
    result += line.body;
  }
  result += m_tail;
  return result;
}

} // namespace deskflow
