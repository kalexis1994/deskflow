/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/IniFile.h"
#include "common/NetworkProtocol.h"

#include <string>
#include <string_view>
#include <vector>

//! The settings the core and daemon read, without Qt
/*!
Reads and writes the same file as the GUI (`Deskflow.conf`, QSettings' INI format)
with the same keys and defaults. Unlike the GUI's `Settings` it never rewrites the
file on its own (no upgrading or cleaning of old keys); it writes only the keys it
is asked to set. The file is re-read when it changes on disk, so a value set by the
GUI or the Tauri app is seen on the next read.
*/
class CoreSettings
{
public:
  using Value = deskflow::SettingValue;

  struct Client
  {
    static constexpr std::string_view DynamicConnectionRetry = "client/dynamicConnectionInterval";
    static constexpr std::string_view InvertYScroll = "client/invertYScroll";
    static constexpr std::string_view InvertXScroll = "client/invertXScroll";
    static constexpr std::string_view YScrollScale = "client/yScrollScale";
    static constexpr std::string_view XScrollScale = "client/xScrollScale";
    static constexpr std::string_view LanguageSync = "client/languageSync";
    static constexpr std::string_view RemoteHost = "client/remoteHost";
    static constexpr std::string_view XdpClipboardRetried = "client/xdpClipboardRetried";
    static constexpr std::string_view XdpRestoreToken = "client/xdpRestoreToken";
  };
  struct Core
  {
    static constexpr std::string_view CoreMode = "core/coreMode";
    static constexpr std::string_view Interface = "core/interface";
    static constexpr std::string_view Port = "core/port";
    static constexpr std::string_view PreventSleep = "core/preventSleep";
    static constexpr std::string_view ProcessMode = "core/processMode";
    static constexpr std::string_view ComputerName = "core/computerName";
    static constexpr std::string_view Display = "core/display";
    static constexpr std::string_view UseHooks = "core/useHooks";
    static constexpr std::string_view EnableEnterCommand = "core/enableEnterCommand";
    static constexpr std::string_view ComputerEnterCommand = "core/enterCommand";
    static constexpr std::string_view EnableExitCommand = "core/enableExitCommand";
    static constexpr std::string_view ComputerExitCommand = "core/exitCommand";
  };
  struct Daemon
  {
    static constexpr std::string_view ConfigFile = "daemon/configFile";
    static constexpr std::string_view Elevate = "daemon/elevate";
    static constexpr std::string_view LogFile = "daemon/logFile";
    static constexpr std::string_view LogLevel = "daemon/logLevel";
  };
  struct Log
  {
    static constexpr std::string_view File = "log/file";
    static constexpr std::string_view Level = "log/level";
    static constexpr std::string_view ToFile = "log/toFile";
  };
  struct Security
  {
    static constexpr std::string_view CheckPeers = "security/checkPeerFingerprints";
    static constexpr std::string_view Certificate = "security/certificate";
    static constexpr std::string_view TlsEnabled = "security/tlsEnabled";
  };
  struct Server
  {
    static constexpr std::string_view ClipboardSize = "server/clipboardSize";
    static constexpr std::string_view DefaultLockToComputerState = "server/defaultLockToComputerState";
    static constexpr std::string_view DisableLockToComputer = "server/disableLockToComputer";
    static constexpr std::string_view EnableClipboard = "server/enableClipboard";
    static constexpr std::string_view EnableHeartbeat = "server/enableHeartbeat";
    static constexpr std::string_view EnableSwitchDelay = "server/enableSwitchDelay";
    static constexpr std::string_view EnableSwitchDoubleTap = "server/enableSwitchDoubleTap";
    static constexpr std::string_view ExternalConfig = "server/externalConfig";
    static constexpr std::string_view ExternalConfigFile = "server/externalConfigFile";
    static constexpr std::string_view Heartbeat = "server/heartbeat";
    static constexpr std::string_view Protocol = "server/protocol";
    static constexpr std::string_view RelativeMouseMoves = "server/relativeMouseMoves";
    static constexpr std::string_view SwitchDelay = "server/switchDelay";
    static constexpr std::string_view SwitchDoubleTap = "server/switchDoubleTap";
    static constexpr std::string_view Win32KeepForeground = "server/win32KeepForeground";
    static constexpr std::string_view XdpClipboardRetried = "server/xdpClipboardRetried";
    static constexpr std::string_view XdpRestoreToken = "server/xdpRestoreToken";
  };

  //! Per-computer keys, used with computerKey()
  struct Computer
  {
    static constexpr std::string_view Aliases = "aliases";
    static constexpr std::string_view Name = "name";
    static constexpr std::string_view HalfDuplexCapsLock = "halfDuplexCapsLock";
    static constexpr std::string_view HalfDuplexNumLock = "halfDuplexNumLock";
    static constexpr std::string_view HalfDuplexScrollLock = "halfDuplexScrollLock";
    static constexpr std::string_view ModifierAlt = "modifierAlt";
    static constexpr std::string_view ModifierAltGr = "modifierAltGr";
    static constexpr std::string_view ModifierCtrl = "modifierCtrl";
    static constexpr std::string_view ModifierMeta = "modifierMeta";
    static constexpr std::string_view ModifierShift = "modifierShift";
    static constexpr std::string_view ModifierSuper = "modifierSuper";
    static constexpr std::string_view SwitchCornerSize = "switchCornerSize";
    static constexpr std::string_view SwitchCornerTopLeft = "switchCornerTopLeft";
    static constexpr std::string_view SwitchCornerTopRight = "switchCornerTopRight";
    static constexpr std::string_view SwitchCornerBottomLeft = "switchCornerBottomLeft";
    static constexpr std::string_view SwitchCornerBottomRight = "switchCornerBottomRight";
    static constexpr std::string_view WeakX11Focus = "weakX11Focus";
    static constexpr std::string_view XtestIsXineramaUnaware = "xtestIsXineramaUnaware";
  };

  // Written as plain ints, the same values as the GUI's Settings enums
  enum ProcessMode
  {
    Service,
    Desktop
  };

  enum CoreMode
  {
    None,
    Client,
    Server
  };

  //! "computer_<name>/<key>"
  static std::string computerKey(std::string_view computer, std::string_view key);

  //! The directory for the user's settings file ("~/AppData/Roaming/Deskflow" on Windows)
  static std::string userDir();
  //! The machine-wide settings directory ("C:/ProgramData/Deskflow" on Windows)
  static std::string systemDir();

  //! Uses this settings file from now on (the core's `--settings` option)
  static void setSettingsFile(const std::string &settingsFile);

  static Value value(std::string_view key);
  //! Sets a key and saves the file; an invalid value removes the key
  static void setValue(std::string_view key, const Value &value = {});
  static Value defaultValue(std::string_view key);

  static bool isPortableMode();
  static std::string portableSettingsFile();
  static std::string settingsFile();
  static std::string settingsPath();
  static std::string serverConfigFile();
  static std::string tlsDir();
  static std::string tlsTrustedServersDb();
  static std::string tlsTrustedClientsDb();
  static std::string logLevelText();
  static NetworkProtocol networkProtocol();

  //! The names of the computers configured in the GUI (the `computer_*` groups)
  static std::vector<std::string> knownComputers();

  //! A valid computer name from any string, the same cleanup the GUI does
  static std::string cleanComputerName(std::string_view name);

  //! For tests: forget the loaded files, so the next read starts over
  static void reset();
};
