/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/CoreSettings.h"
#include "arch/OsDetection.h"

#include "arch/Utf8.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/KeyboardModifier.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <mutex>

#if defined(DESKFLOW_OS_WIN)
#include <userenv.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#if defined(DESKFLOW_OS_MACOS)
#include <climits>
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;
using deskflow::IniFile;
using deskflow::pathToUtf8;
using deskflow::utf8Path;

namespace {

// clang-format off
// The keys that default to false, and to true (the core-relevant part of the GUI's lists)
constexpr std::array kDefaultFalseValues = {
    CoreSettings::Core::PreventSleep
  , CoreSettings::Core::EnableEnterCommand
  , CoreSettings::Core::EnableExitCommand
  , CoreSettings::Client::DynamicConnectionRetry
  , CoreSettings::Client::InvertYScroll
  , CoreSettings::Client::InvertXScroll
  , CoreSettings::Client::LanguageSync
  , CoreSettings::Client::XdpClipboardRetried
  , CoreSettings::Log::ToFile
  , CoreSettings::Server::DefaultLockToComputerState
  , CoreSettings::Server::DisableLockToComputer
  , CoreSettings::Server::EnableHeartbeat
  , CoreSettings::Server::EnableSwitchDelay
  , CoreSettings::Server::EnableSwitchDoubleTap
  , CoreSettings::Server::ExternalConfig
  , CoreSettings::Server::RelativeMouseMoves
  , CoreSettings::Server::XdpClipboardRetried
};

constexpr std::array kDefaultTrueValues = {
    CoreSettings::Core::UseHooks
  , CoreSettings::Core::ClipboardPreview
  , CoreSettings::Security::TlsEnabled
  , CoreSettings::Security::CheckPeers
  , CoreSettings::Server::EnableClipboard
  , CoreSettings::Server::Win32KeepForeground
};

// Settings kept in the state file rather than the settings file
constexpr std::array kStateKeys = {
    CoreSettings::Client::XdpClipboardRetried
  , CoreSettings::Client::XdpRestoreToken
  , CoreSettings::Server::XdpClipboardRetried
  , CoreSettings::Server::XdpRestoreToken
};
// clang-format on

template <typename List> bool contains(const List &list, std::string_view key)
{
  return std::find(list.begin(), list.end(), key) != list.end();
}

std::string environment(const char *name)
{
#if defined(DESKFLOW_OS_WIN)
  const auto wideName = deskflow::utf8ToWide(name);
  const auto size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
  if (size == 0)
    return {};
  std::wstring value(size, L'\0');
  const auto written = GetEnvironmentVariableW(wideName.c_str(), value.data(), size);
  value.resize(written);
  return deskflow::wideToUtf8(value);
#else
  const char *value = std::getenv(name);
  return value ? value : std::string();
#endif
}

//! Forward slashes, as Qt returns paths
std::string withSlashes(std::string path)
{
#if defined(DESKFLOW_OS_WIN)
  std::replace(path.begin(), path.end(), '\\', '/');
#endif
  return path;
}

bool exists(const std::string &path)
{
  std::error_code ec;
  return !path.empty() && fs::exists(utf8Path(path), ec);
}

bool isDirectory(const std::string &path)
{
  std::error_code ec;
  return !path.empty() && fs::is_directory(utf8Path(path), ec);
}

std::string absolutePath(const std::string &path)
{
  std::error_code ec;
  auto absolute = fs::absolute(utf8Path(path), ec);
  if (ec)
    absolute = utf8Path(path);
  return withSlashes(pathToUtf8(absolute.lexically_normal()));
}

std::string parentPath(const std::string &path)
{
  return withSlashes(pathToUtf8(utf8Path(path).parent_path()));
}

#if defined(DESKFLOW_OS_WIN)
//! What Qt's QDir::rootPath() returns on Windows: the system drive
std::string rootPath()
{
  auto drive = environment("SystemDrive");
  if (drive.empty())
    drive = "c:";
  return drive + "/";
}
#endif

//! The same home directory as Qt's QDir::homePath()
std::string homePath()
{
#if defined(DESKFLOW_OS_WIN)
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD size = 0;
    GetUserProfileDirectoryW(token, nullptr, &size);
    std::wstring profile(size, L'\0');
    const bool ok = size != 0 && GetUserProfileDirectoryW(token, profile.data(), &size);
    CloseHandle(token);
    if (ok) {
      profile.resize(wcslen(profile.c_str()));
      if (const auto result = deskflow::wideToUtf8(profile); isDirectory(result))
        return withSlashes(result);
    }
  }
  for (const auto &candidate :
       {environment("USERPROFILE"), environment("HOMEDRIVE") + environment("HOMEPATH"), environment("HOME")}) {
    if (isDirectory(candidate))
      return withSlashes(candidate);
  }
  return rootPath();
#else
  if (auto home = environment("HOME"); !home.empty())
    return home;
  if (const auto *pw = getpwuid(getuid()); pw && pw->pw_dir)
    return pw->pw_dir;
  return "/";
#endif
}

//! The directory of the running program, as QCoreApplication::applicationDirPath()
std::string applicationDirPath()
{
  std::string program;
#if defined(DESKFLOW_OS_WIN)
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return {};
    if (length < buffer.size()) {
      buffer.resize(length);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  program = deskflow::wideToUtf8(buffer);
#elif defined(DESKFLOW_OS_MACOS)
  char buffer[PATH_MAX];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) != 0)
    return {};
  char resolved[PATH_MAX];
  program = realpath(buffer, resolved) ? resolved : buffer;
#else
  std::error_code ec;
  const auto self = fs::read_symlink("/proc/self/exe", ec);
  if (ec)
    return {};
  program = self.string();
#endif
  return parentPath(program);
}

std::string hostName()
{
#if defined(DESKFLOW_OS_WIN)
  DWORD size = 0;
  GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
  std::wstring name(size, L'\0');
  if (size == 0 || !GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size))
    return {};
  name.resize(size);
  return deskflow::wideToUtf8(name);
#else
  std::array<char, 256> name{};
  if (gethostname(name.data(), name.size() - 1) != 0)
    return {};
  return name.data();
#endif
}

std::string userSettingsFile()
{
  return CoreSettings::userDir() + "/" + kAppName + ".conf";
}

std::string systemSettingsFile()
{
  return CoreSettings::systemDir() + "/" + kAppName + ".conf";
}

//! The file the GUI's Settings picks when nothing else is asked for
std::string defaultSettingsFile()
{
#if defined(DESKFLOW_OS_WIN)
  if (const auto portable = CoreSettings::portableSettingsFile(); exists(portable))
    return portable;
#else
  if (const auto xdgConfigHome = environment("XDG_CONFIG_HOME"); !xdgConfigHome.empty())
    return xdgConfigHome + "/" + kAppName + "/" + kAppName + ".conf";
#endif
  if (const auto user = userSettingsFile(); exists(user))
    return user;
  if (const auto system = systemSettingsFile(); exists(system))
    return system;
  return userSettingsFile();
}

//! Where Qt's GenericStateLocation points, for the state file
std::string stateFile()
{
  auto base = environment("XDG_STATE_HOME");
  if (base.empty()) {
#if defined(DESKFLOW_OS_WIN)
    base = environment("LOCALAPPDATA");
    if (base.empty())
      base = homePath() + "/AppData/Local";
    base = withSlashes(base);
#elif defined(DESKFLOW_OS_MACOS)
    base = homePath() + "/Library/Preferences";
#else
    base = homePath() + "/.local/state";
#endif
  }
  return base + "/" + kAppName + ".state";
}

//! A settings file and its contents, re-read when it changes on disk
struct LoadedFile
{
  std::string path;
  IniFile ini;
  fs::file_time_type modified;
  std::uintmax_t size = 0;
  bool loaded = false;

  IniFile &current()
  {
    std::error_code ec;
    const auto file = utf8Path(path);
    auto time = fs::last_write_time(file, ec);
    if (ec)
      time = {};
    auto bytes = fs::file_size(file, ec);
    if (ec)
      bytes = 0;
    if (!loaded || time != modified || bytes != size) {
      ini = IniFile::load(file);
      modified = time;
      size = bytes;
      loaded = true;
    }
    return ini;
  }

  bool save()
  {
    const bool saved = ini.save(utf8Path(path));
    loaded = false; // pick up the new timestamp on the next read
    return saved;
  }
};

struct State
{
  std::recursive_mutex mutex;
  LoadedFile settings;
  LoadedFile state;
};

State &state()
{
  static State s;
  return s;
}

LoadedFile &settingsFileLocked()
{
  auto &s = state();
  if (s.settings.path.empty())
    s.settings.path = absolutePath(defaultSettingsFile());
  return s.settings;
}

LoadedFile &fileFor(std::string_view key)
{
  auto &s = state();
  if (contains(kStateKeys, key) && !CoreSettings::isPortableMode()) {
    if (s.state.path.empty())
      s.state.path = stateFile();
    return s.state;
  }
  return settingsFileLocked();
}

CoreSettings::Value computerDefaults(std::string_view key)
{
  using enum KeyboardModifier;
  if (key.find("halfDuplex") != std::string_view::npos ||
      key.find("xtestIsXineramaUnaware") != std::string_view::npos || key.find("X11") != std::string_view::npos ||
      key.find("Left") != std::string_view::npos || key.find("Right") != std::string_view::npos)
    return false;
  if (key.ends_with("switchCornerSize"))
    return 0;
  if (key == CoreSettings::Computer::ModifierAlt)
    return keyboardModifierToOption(Alt);
  if (key == CoreSettings::Computer::ModifierAltGr)
    return keyboardModifierToOption(AltGr);
  if (key == CoreSettings::Computer::ModifierCtrl)
    return keyboardModifierToOption(Ctrl);
  if (key == CoreSettings::Computer::ModifierMeta)
    return keyboardModifierToOption(Meta);
  if (key == CoreSettings::Computer::ModifierShift)
    return keyboardModifierToOption(Shift);
  if (key == CoreSettings::Computer::ModifierSuper)
    return keyboardModifierToOption(Super);
  return {};
}

} // namespace

std::string CoreSettings::computerKey(std::string_view computer, std::string_view key)
{
  std::string result = "computer_";
  result.append(computer);
  result += '/';
  result.append(key);
  return result;
}

std::string CoreSettings::userDir()
{
#if defined(DESKFLOW_OS_WIN)
  return homePath() + "/AppData/Roaming/" + kAppName;
#elif defined(DESKFLOW_OS_MACOS)
  return homePath() + "/Library/" + kAppName;
#else
  return homePath() + "/.config/" + kAppName;
#endif
}

std::string CoreSettings::systemDir()
{
#if defined(DESKFLOW_OS_WIN)
  return rootPath() + "ProgramData/" + kAppName;
#elif defined(DESKFLOW_OS_MACOS)
  return std::string("/Library/") + kAppName;
#else
  return std::string("/etc/") + kAppName;
#endif
}

void CoreSettings::setSettingsFile(const std::string &settingsFile)
{
  std::scoped_lock lock{state().mutex};
  auto &file = state().settings;
  const auto path = absolutePath(settingsFile);
  if (file.path == path) {
    LOG_DEBUG("settings file already set, skipping");
    return;
  }
  file.path = path;
  file.loaded = false;
  LOG_INFO("settings file changed: %s", path.c_str());
}

CoreSettings::Value CoreSettings::value(std::string_view key)
{
  std::scoped_lock lock{state().mutex};
  return fileFor(key).current().value(key, defaultValue(key));
}

void CoreSettings::setValue(std::string_view key, const Value &value)
{
  std::scoped_lock lock{state().mutex};
  auto &file = fileFor(key);
  auto &ini = file.current();

  if (!value.isValid()) {
    if (!ini.contains(key))
      return;
    ini.remove(key);
  } else {
    const auto stored = key == Core::ComputerName ? Value(cleanComputerName(value.toString())) : value;
    if (ini.contains(key) && ini.value(key).toIni() == stored.toIni())
      return;
    ini.setValue(key, stored);
  }

  if (!file.save())
    LOG_WARN("settings not saved, could not write: %s", file.path.c_str());
}

CoreSettings::Value CoreSettings::defaultValue(std::string_view key)
{
  if (key.starts_with("computer_"))
    return computerDefaults(key.substr(key.find('/') + 1));

  if (contains(kDefaultFalseValues, key))
    return false;

  if (contains(kDefaultTrueValues, key))
    return true;

  if (key == Security::Certificate)
    return tlsDir() + "/" + kAppId + ".pem";

  if (key == Log::File)
    return homePath() + "/" + kAppId + ".log";

  if (key == Log::Level)
    return "Info";

  if (key == Daemon::Elevate)
    return !isPortableMode();

  if (key == Server::ExternalConfigFile)
    return settingsPath() + "/" + kAppId + "-server.conf";

  if (key == Core::Port)
    return 24800;

  if (key == Core::ProcessMode) {
#if defined(DESKFLOW_OS_WIN)
    if (!isPortableMode())
      return static_cast<int>(ProcessMode::Service);
#endif
    return static_cast<int>(ProcessMode::Desktop);
  }

  // The GUI writes this when it starts; without a GUI the core still has a name
  if (key == Core::ComputerName)
    return cleanComputerName(hostName());

  if (key == Daemon::LogFile)
    return settingsPath() + "/" + kAppId + "-daemon.log";

  if (key == Client::YScrollScale || key == Client::XScrollScale)
    return 1.0;

  if (key == Server::Protocol)
    return networkProtocolToOption(NetworkProtocol::Barrier);

  if (key == Server::Heartbeat)
    return 5000;

  if (key == Server::SwitchDelay || key == Server::SwitchDoubleTap)
    return 250;

  if (key == Server::ClipboardSize)
    return 3; // 3 MiB

  return {};
}

bool CoreSettings::isPortableMode()
{
  // portable mode only if the portable settings file exists in the expected location
  return exists(portableSettingsFile());
}

std::string CoreSettings::portableSettingsFile()
{
  static const auto file = absolutePath(applicationDirPath() + "/settings/" + kAppName + ".conf");
  return file;
}

std::string CoreSettings::settingsFile()
{
  std::scoped_lock lock{state().mutex};
  return settingsFileLocked().path;
}

std::string CoreSettings::settingsPath()
{
#if defined(DESKFLOW_OS_WIN)
  if (!isPortableMode())
    return systemDir();
#endif
  return parentPath(settingsFile());
}

std::string CoreSettings::serverConfigFile()
{
  const bool useExternal = value(Server::ExternalConfig).toBool();
  return useExternal ? value(Server::ExternalConfigFile).toString()
                     : defaultValue(Server::ExternalConfigFile).toString();
}

std::string CoreSettings::tlsDir()
{
  return settingsPath() + "/tls";
}

std::string CoreSettings::tlsTrustedServersDb()
{
  return tlsDir() + "/trusted-servers";
}

std::string CoreSettings::tlsTrustedClientsDb()
{
  return tlsDir() + "/trusted-clients";
}

std::string CoreSettings::logLevelText()
{
  return value(Log::Level).toString();
}

NetworkProtocol CoreSettings::networkProtocol()
{
  return networkProtocolFromString(value(Server::Protocol).toString());
}

std::vector<std::string> CoreSettings::knownComputers()
{
  std::scoped_lock lock{state().mutex};
  const auto &ini = settingsFileLocked().current();
  std::vector<std::string> computers;
  for (const auto &group : ini.childGroups()) {
    if (group.starts_with("computer_"))
      computers.push_back(ini.value(group + "/name").toString());
  }
  return computers;
}

std::string CoreSettings::cleanComputerName(std::string_view name)
{
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  const auto isEdge = [](char c) { return c == '-' || c == '_' || c == '.'; };
  const auto isAllowed = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           c == '.';
  };

  // QString::simplified, then spaces to underscores: each run of whitespace becomes one '_'
  std::string spaced;
  bool pendingSpace = false;
  for (const char c : name) {
    if (isSpace(c)) {
      pendingSpace = !spaced.empty();
      continue;
    }
    if (pendingSpace)
      spaced += '_';
    pendingSpace = false;
    spaced += c;
  }

  std::string clean;
  std::copy_if(spaced.begin(), spaced.end(), std::back_inserter(clean), isAllowed);

  const auto first = std::find_if_not(clean.begin(), clean.end(), isEdge);
  clean.erase(clean.begin(), first);
  while (!clean.empty() && isEdge(clean.back()))
    clean.pop_back();

  if (clean.size() > 255) {
    clean.resize(255);
    return cleanComputerName(clean);
  }
  return clean;
}

void CoreSettings::reset()
{
  std::scoped_lock lock{state().mutex};
  state().settings = {};
  state().state = {};
}
