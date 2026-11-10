/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2003 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/Computer.h"
#include "arch/OsDetection.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/CoreSettings.h"
#include "deskflow/IPlatformComputer.h"
#include <cassert>

#include <string>
#include <vector>

#ifdef DESKFLOW_OS_WIN
#include "arch/Utf8.h"
#include "arch/win32/ArchMiscWindows.h"
#include "platform/MSWindowsProcess.h"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace deskflow {

namespace {

#ifndef DESKFLOW_OS_WIN
//! Splits a command line the way QProcess::splitCommand did: spaces separate
//! arguments, double quotes group them, and three quotes in a row are one literal quote.
std::vector<std::string> splitCommand(const std::string &command)
{
  std::vector<std::string> args;
  std::string arg;
  int quoteCount = 0;
  bool inQuote = false;

  for (const char c : command) {
    if (c == '"') {
      ++quoteCount;
      if (quoteCount == 3) {
        quoteCount = 0;
        arg += c;
      }
      continue;
    }
    if (quoteCount) {
      if (quoteCount == 1)
        inQuote = !inQuote;
      quoteCount = 0;
    }
    if (!inQuote && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
      if (!arg.empty()) {
        args.push_back(arg);
        arg.clear();
      }
    } else {
      arg += c;
    }
  }
  if (!arg.empty())
    args.push_back(arg);
  return args;
}

//! Starts a program that outlives us and isn't our child (as QProcess::startDetached)
bool startDetached(const std::vector<std::string> &args)
{
  int errorPipe[2];
  if (::pipe(errorPipe) != 0)
    return false;
  fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC);

  const pid_t child = ::fork();
  if (child < 0) {
    ::close(errorPipe[0]);
    ::close(errorPipe[1]);
    return false;
  }

  if (child == 0) {
    // the intermediate child exits at once, so the program is adopted by init
    ::close(errorPipe[0]);
    ::setsid();
    if (::fork() == 0) {
      std::vector<char *> argv;
      for (const auto &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
      argv.push_back(nullptr);
      ::execvp(argv[0], argv.data());
      const int error = errno;
      [[maybe_unused]] const auto ignored = ::write(errorPipe[1], &error, sizeof(error));
      ::_exit(127);
    }
    ::_exit(0);
  }

  ::close(errorPipe[1]);
  int status = 0;
  ::waitpid(child, &status, 0);
  int execError = 0;
  const auto bytes = ::read(errorPipe[0], &execError, sizeof(execError)); // EOF once exec succeeded
  ::close(errorPipe[0]);
  return bytes == 0;
}
#endif

bool runComputerCommand(const std::string &commandLine)
{
#ifdef DESKFLOW_OS_WIN
  using deskflow::platform::MSWindowsProcess;
  if (ArchMiscWindows::isProcessElevated()) {
    LOG_DEBUG("current process is elevated, starting detached process as session user");
    return MSWindowsProcess::startDetachedAsSessionUser(deskflow::utf8ToWide(commandLine));
  }

  // Windows parses the command line itself, so it runs exactly as the user wrote it
  auto command = deskflow::utf8ToWide(commandLine);
  if (command.empty())
    return false;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const DWORD flags = (GetConsoleWindow() ? 0 : CREATE_NO_WINDOW) | CREATE_UNICODE_ENVIRONMENT;
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &startup, &process))
    return false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
#else
  const auto args = splitCommand(commandLine);
  if (args.empty()) {
    return false;
  }
  return startDetached(args);
#endif
}

} // namespace

//
// Computer
//

Computer::Computer(IPlatformComputer *platformComputer, IEventQueue *events)
    : m_computer(platformComputer),
      m_isPrimary(platformComputer->isPrimary()),
      m_entered(m_isPrimary),
      m_events(events)
{
  assert(m_computer != nullptr);

  // reset options
  resetOptions();

  LOG_DEBUG("opened display");
}

Computer::~Computer()
{

  if (m_enabled) {
    disable();
  }
  assert(!m_enabled);

  // Originally there was an assert here added before 2009 (history not in
  // tact). This condition seems to occur on a Windows client when the process
  // is shut down to make way for a new elevated process (e.g. at login screen).
  // The reason why this assert was originally added is unclear, and was causing
  // pain when using debug builds; you lose control of the client when it's at
  // the login screen. Therefore it has been converted to a warning so that we
  // can still see when it happens but it won't cause the process to pause. This
  // also gives us the added benefit of seeing when it happens in production.
  // Perhaps it indicates that the cursor is still being controlled on the
  // client while it's shutting down? i.e. the computer is entered and is not the
  // server, or the computer is not entered and is the server.
  if (m_entered != m_isPrimary) {
    LOG(
        (CLOG_DEBUG "current computer: entered=%s, primary=%s", //
         m_entered ? "yes" : "no", m_isPrimary ? "yes" : "no")
    );
    if (m_isPrimary) {
      LOG_WARN("current primary computer is not entered on shutdown");
    } else {
      LOG_WARN("current secondary computer is entered on shutdown");
    }
  }

  delete m_computer;
  LOG_DEBUG("closed display");
}

void Computer::enable()
{
  assert(!m_enabled);

  m_computer->updateKeyMap();
  m_computer->updateKeyState();
  m_computer->enable();
  if (m_isPrimary) {
    enablePrimary();
  }

  // note activation
  m_enabled = true;
}

void Computer::disable()
{
  assert(m_enabled);

  if (!m_isPrimary && m_entered) {
    leave();
  } else if (m_isPrimary && !m_entered) {
    enter(0);
  }
  m_computer->disable();
  if (m_isPrimary) {
    disablePrimary();
  } else {
    disableSecondary();
  }

  // note deactivation
  m_enabled = false;
}

void Computer::enter(KeyModifierMask toggleMask)
{
  LOG_INFO("entering computer");

  if (m_entered) {
    LOG_WARN("computer already entered");
  }

  // now on computer
  m_entered = true;

  m_computer->enter();
  if (m_isPrimary) {
    enterPrimary();
  } else {
    enterSecondary(toggleMask);
  }

  if (CoreSettings::value(CoreSettings::Core::EnableEnterCommand).toBool()) {
    const auto commandLine = CoreSettings::value(CoreSettings::Core::ComputerEnterCommand).toString();
    LOG_DEBUG("running computer enter command: %s", commandLine.c_str());
    if (!runComputerCommand(commandLine))
      LOG_ERR("failed to run computer enter command");
  }
}

bool Computer::leave()
{
  LOG_INFO("leaving computer");

  if (!m_entered) {
    LOG_WARN("computer already left");
  }

  if (!m_computer->canLeave()) {
    return false;
  }

  if (m_isPrimary) {
    leavePrimary();
  } else {
    leaveSecondary();
  }

  m_computer->leave();
  if (CoreSettings::value(CoreSettings::Core::EnableExitCommand).toBool()) {
    const auto commandLine = CoreSettings::value(CoreSettings::Core::ComputerExitCommand).toString();
    LOG_DEBUG("running computer exit command: %s", commandLine.c_str());
    if (!runComputerCommand(commandLine))
      LOG_ERR("failed to run computer exit command");
  }

  // make sure our idea of clipboard ownership is correct
  m_computer->checkClipboards();

  // now not on computer
  m_entered = false;

  return true;
}

void Computer::reconfigure(uint32_t activeSides)
{
  assert(m_isPrimary);
  m_computer->reconfigure(activeSides);
}

void Computer::warpCursor(int32_t x, int32_t y)
{
  assert(m_isPrimary);
  m_computer->warpCursor(x, y);
}

bool Computer::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  return m_computer->setClipboard(id, clipboard);
}

std::vector<deskflow::Monitor> Computer::getMonitors() const
{
  return m_computer->getMonitors();
}

bool Computer::isMouseCaptured() const
{
  return m_computer->isMouseCaptured();
}

bool Computer::canShareFiles() const
{
  return m_computer->canShareFiles();
}

std::optional<std::vector<std::filesystem::path>> Computer::getClipboardFiles() const
{
  return m_computer->getClipboardFiles();
}

bool Computer::setClipboardFiles(const std::vector<std::filesystem::path> &files)
{
  return m_computer->setClipboardFiles(files);
}

void Computer::grabClipboard(ClipboardID id)
{
  m_computer->setClipboard(id, nullptr);
}

void Computer::screensaver(bool) const
{
  // do nothing
}

void Computer::keyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang)
{
  // check for ctrl+alt+del emulation
  if (id == kKeyDelete && (mask & (KeyModifierControl | KeyModifierAlt)) == (KeyModifierControl | KeyModifierAlt)) {
    LOG_DEBUG("emulating ctrl+alt+del press");
    if (m_computer->fakeCtrlAltDel()) {
      return;
    }
  }
  m_computer->fakeKeyDown(id, mask, button, lang);
}

void Computer::keyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang)
{
  assert(!m_isPrimary);
  m_computer->fakeKeyRepeat(id, mask, count, button, lang);
}

void Computer::keyUp(KeyID, KeyModifierMask, KeyButton button)
{
  m_computer->fakeKeyUp(button);
}

void Computer::mouseDown(ButtonID button)
{
  m_computer->fakeMouseButton(button, true);
}

void Computer::mouseUp(ButtonID button)
{
  m_computer->fakeMouseButton(button, false);
}

void Computer::mouseMove(int32_t x, int32_t y)
{
  assert(!m_isPrimary);
  m_computer->fakeMouseMove(x, y);
}

void Computer::mouseRelativeMove(int32_t dx, int32_t dy) const
{
  assert(!m_isPrimary);
  m_computer->fakeMouseRelativeMove(dx, dy);
}

void Computer::mouseWheel(int32_t xDelta, int32_t yDelta) const
{
  assert(!m_isPrimary);
  m_computer->fakeMouseWheel({xDelta, yDelta});
}

void Computer::resetOptions()
{
  // reset options
  m_halfDuplex = 0;

  // let screen handle its own options
  m_computer->resetOptions();
}

void Computer::setOptions(const OptionsList &options)
{
  if (options.size() % 2 != 0) {
    LOG_ERR("options are the incorrect size, can not process them");
    return;
  }

  // update options
  for (uint32_t i = 0, n = (uint32_t)options.size(); i < n; i += 2) {
    if (options[i] == kOptionHalfDuplexCapsLock) {
      if (options[i + 1] != 0) {
        m_halfDuplex |= KeyModifierCapsLock;
      } else {
        m_halfDuplex &= ~KeyModifierCapsLock;
      }
      LOG_VERBOSE("half-duplex caps-lock %s", ((m_halfDuplex & KeyModifierCapsLock) != 0) ? "on" : "off");
    } else if (options[i] == kOptionHalfDuplexNumLock) {
      if (options[i + 1] != 0) {
        m_halfDuplex |= KeyModifierNumLock;
      } else {
        m_halfDuplex &= ~KeyModifierNumLock;
      }
      LOG_VERBOSE("half-duplex num-lock %s", ((m_halfDuplex & KeyModifierNumLock) != 0) ? "on" : "off");
    } else if (options[i] == kOptionHalfDuplexScrollLock) {
      if (options[i + 1] != 0) {
        m_halfDuplex |= KeyModifierScrollLock;
      } else {
        m_halfDuplex &= ~KeyModifierScrollLock;
      }
      LOG_VERBOSE("half-duplex scroll-lock %s", ((m_halfDuplex & KeyModifierScrollLock) != 0) ? "on" : "off");
    }
  }

  // update half-duplex options
  m_computer->setHalfDuplexMask(m_halfDuplex);

  // let screen handle its own options
  m_computer->setOptions(options);
}

void Computer::setSequenceNumber(uint32_t seqNum)
{
  m_computer->setSequenceNumber(seqNum);
}

uint32_t Computer::registerHotKey(KeyID key, KeyModifierMask mask)
{
  return m_computer->registerHotKey(key, mask);
}

void Computer::unregisterHotKey(uint32_t id)
{
  m_computer->unregisterHotKey(id);
}

void Computer::fakeInputBegin()
{
  assert(!m_fakeInput);

  m_fakeInput = true;
  m_computer->fakeInputBegin();
}

void Computer::fakeInputEnd()
{
  assert(m_fakeInput);

  m_fakeInput = false;
  m_computer->fakeInputEnd();
}

bool Computer::isOnComputer() const
{
  return m_entered;
}

bool Computer::isLockedToComputer() const
{
  if (uint32_t buttonID = 0; m_computer->isAnyMouseButtonDown(buttonID)) {
    LOG_DEBUG("locked by mouse buttonID: %d", buttonID);
    return true;
  }
  // not locked
  return false;
}

int32_t Computer::getJumpZoneSize() const
{
  if (!m_isPrimary) {
    return 0;
  } else {
    return m_computer->getJumpZoneSize();
  }
}

void Computer::getCursorCenter(int32_t &x, int32_t &y) const
{
  m_computer->getCursorCenter(x, y);
}

KeyModifierMask Computer::getActiveModifiers() const
{
  return m_computer->getActiveModifiers();
}

KeyModifierMask Computer::pollActiveModifiers() const
{
  return m_computer->pollActiveModifiers();
}

void *Computer::getEventTarget() const
{
  return m_computer;
}

bool Computer::getClipboard(ClipboardID id, IClipboard *clipboard) const
{
  return m_computer->getClipboard(id, clipboard);
}

void Computer::getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const
{
  m_computer->getShape(x, y, w, h);
}

void Computer::getCursorPos(int32_t &x, int32_t &y) const
{
  m_computer->getCursorPos(x, y);
}

void Computer::enablePrimary()
{
  // get notified of screen saver activation/deactivation
  m_computer->openScreensaver(true);

  // claim computer changed size
  m_events->addEvent(Event(EventTypes::ComputerShapeChanged, getEventTarget()));
}

void Computer::disablePrimary()
{
  // done with screen saver
  m_computer->closeScreensaver();
}

void Computer::disableSecondary()
{
  // done with screen saver
  m_computer->closeScreensaver();
}

void Computer::enterPrimary() const
{
  // do nothing
}

void Computer::enterSecondary(KeyModifierMask) const
{
  // do nothing
}

void Computer::leavePrimary()
{
  // we don't track keys while on the primary computer so update our
  // idea of them now.  this is particularly to update the state of
  // the toggle modifiers.
  m_computer->updateKeyState();
}

void Computer::leaveSecondary()
{
  // release any keys we think are still down
  m_computer->fakeAllKeysUp();
}

std::string Computer::getSecureInputApp() const
{
  return m_computer->getSecureInputApp();
}

} // namespace deskflow
