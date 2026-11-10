/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/App.h"
#include "arch/OsDetection.h"
#include <cassert>

#include "DisplayInvalidException.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include "base/LogOutputters.h"
#include "base/RecentLogOutputter.h"
#include "common/CoreSettings.h"
#include "common/ExitCodes.h"
#include "deskflow/DeskflowException.h"
#include "mt/ThreadException.h"
#include "net/SecureUtils.h"

#if defined(DESKFLOW_OS_WIN)
#include "base/IEventQueue.h"
#endif

#include <stdexcept>

#if defined(DESKFLOW_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

#if defined(WINAPI_XWINDOWS) or defined(WINAPI_LIBEI)
#include "platform/XDGPortalRegistry.h"
#endif

using namespace deskflow;

App *App::s_instance = nullptr;

void App::ensureTlsCertificate()
{
  if (!CoreSettings::value(CoreSettings::Security::TlsEnabled).toBool())
    return;
  const auto path = CoreSettings::value(CoreSettings::Security::Certificate).toString();
  if (path.empty())
    return;
  try {
    if (deskflow::ensurePemSelfSignedCert(path))
      LOG_INFO("created this computer's tls certificate: %s", path.c_str());
  } catch (const std::exception &e) {
    LOG_ERR("could not create this computer's tls certificate %s: %s", path.c_str(), e.what());
  }
}

//
// App
//

App::App(IEventQueue *events, const std::string &processName)
    : m_bye(&exit),
      m_events(events),
      m_appUtil(events),
      m_pname(processName)
{
  assert(s_instance == nullptr);
  s_instance = this;
#if defined(WINAPI_XWINDOWS) or defined(WINAPI_LIBEI)
  deskflow::platform::setAppId();
#endif
}

App::~App()
{
  s_instance = nullptr;
}

void App::run()
{
  LOG_INFO("starting core");

#if MAC_OS_X_VERSION_10_7
  // dock hide only supported on lion :(
  ProcessSerialNumber psn = {0, kCurrentProcess};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  GetCurrentProcess(&psn);
#pragma GCC diagnostic pop

  TransformProcessType(&psn, kProcessTransformToBackgroundApplication);
#endif

  // install application in to arch
  appUtil().adoptApp(this);

  // HACK: fail by default (saves us setting result in each catch)
  int result = s_exitFailed;

  try {
    result = appUtil().run();
  } catch (ExitAppException &e) {
    // instead of showing a nasty error, just exit with the error code.
    // not sure if i like this behaviour, but it's probably better than
    // using the exit(int) function!
    result = e.getCode();
  } catch (DisplayInvalidException &die) {
    LOG_CRIT("a display invalid exception error occurred: %s\n", die.what());
    // display invalid exceptions can occur when going to sleep. When this
    // process exits, the UI will restart us instantly. We don't really want
    // that behevior, so we quies for a bit
    Arch::sleep(10);
  } catch (std::runtime_error &re) {
    LOG_CRIT("a runtime error occurred: %s\n", re.what());
  } catch (std::exception &e) {
    LOG_CRIT("an error occurred: %s\n", e.what());
  } catch (...) {
    LOG_CRIT("an unknown error occurred\n");
  }

  if (result == s_exitSuccess) {
    LOG_INFO("core stopped successfully");
  } else {
    updateExitCode(result);
    LOG_ERR("core stopped with error code: %d", result);
  }
}

void App::setupFileLogging()
{
  // the newest lines, which a server shows its clients when they ask
  CLOG->insert(new RecentLogOutputter, true); // NOSONAR - Adopted by `Log`

  if (CoreSettings::value(CoreSettings::Log::ToFile).toBool()) {
    const auto file = CoreSettings::value(CoreSettings::Log::File).toString();
    m_fileLog = new FileLogOutputter(file); // NOSONAR - Adopted by `Log`
    CLOG->insert(m_fileLog);
    LOG_VERBOSE("logging to file (%s) enabled", file.c_str());
  }
}

void App::loggingFilterWarning() const
{
  if ((CLOG->getFilter() > CLOG->getConsoleMaxLevel()) && (CoreSettings::value(CoreSettings::Log::ToFile).toBool())) {
    LOG_WARN(
        "log messages above %s are NOT sent to console (use file logging)",
        LogLevel::toOption(CLOG->getConsoleMaxLevel()).c_str()
    );
  }
}

void App::initApp()
{
  parseArgs();

  // set log filter
  if (const auto logLevel = CoreSettings::logLevelText(); !CLOG->setFilter(logLevel)) {
    LOG_CRIT("%s: unrecognized log level `%s'" BYE, processName().c_str(), logLevel.c_str(), processName().c_str());
    m_bye(s_exitArgs);
  }
  loggingFilterWarning();

  // setup file logging after parsing args
  setupFileLogging();

  // load configuration
  loadConfig();
}

void App::handleComputerError() const
{
  LOG_CRIT("error on computer");
  getEvents()->addEvent(Event(EventTypes::Quit));
}

void App::quit() const
{
  LOG_INFO("quitting");
  getEvents()->addEvent(Event(EventTypes::Quit));
}

void App::runEventsLoop(const void *)
{
  int exitCode = m_events->loop();
  if (exitCode != s_exitSuccess) {
    throw ThreadExitException(new LoopErrorCode(exitCode));
  }
}
