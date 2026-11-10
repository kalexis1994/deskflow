/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2025 - 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreArgParser.h"
#include "arch/OsDetection.h"

#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "deskflow/ClientApp.h"
#include "deskflow/ProcessUtils.h"
#include "deskflow/ServerApp.h"
#include "deskflow/ipc/CoreIpcServer.h"

#if defined(DESKFLOW_OS_WIN)
#include "arch/win32/ArchMiscWindows.h"
#include "platform/MSWindowsClipboardFiles.h"
#endif

#if defined(DESKFLOW_OS_MACOS)
#include "platform/OSXCocoaApp.h"
#include <CoreFoundation/CoreFoundation.h>
#endif

#if defined(DESKFLOW_CORE_QT_EVENT_LOOP)
#include <QGuiApplication>
#include <QTimer>
#endif

#include <atomic>
#include <cstdio>
#include <string_view>
#include <thread>

namespace {

#if defined(DESKFLOW_CORE_QT_EVENT_LOOP)
void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const auto utf8 = message.toUtf8();
  switch (type) {
  case QtDebugMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_DEBUG "%s", utf8.constData());
    break;
  case QtInfoMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_INFO "%s", utf8.constData());
    break;
  case QtWarningMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_WARN "%s", utf8.constData());
    break;
  case QtCriticalMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_ERR "%s", utf8.constData());
    break;
  case QtFatalMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_CRIT "%s", utf8.constData());
    break;
  }
  if (type == QtFatalMsg) {
    abort();
  }
}
#endif

//! What the main thread does while the core runs on its own thread
/*!
The core used to run under QApplication, whose event loop kept the main thread busy.
On macOS the main thread now runs its CFRunLoop for the same reason (NSApp exists as
before). On Linux the portal clipboard (QImage) and the DBus helpers still use Qt,
which wants its application object, as before a GUI one; see docs/dev/remove-qt.md,
phase 5b. Elsewhere it just waits.
*/
void runMainThreadUntil(const std::atomic<bool> &done)
{
#if defined(DESKFLOW_CORE_QT_EVENT_LOOP)
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, [&done] {
    if (done)
      QCoreApplication::quit();
  });
  poll.start(100);
  QCoreApplication::exec();
#elif defined(DESKFLOW_OS_MACOS)
  while (!done)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
#else
  (void)done;
#endif
}

App *createApp(const CoreArgParser &parser, EventQueue &events, const std::string &processName)
{
  if (parser.serverMode()) {
    return new ServerApp(&events, processName);
  } else if (parser.clientMode()) {
    return new ClientApp(&events, processName);
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
#if defined(DESKFLOW_OS_WIN)
  // the core, when it's SYSTEM, asking a process of the user what the user copied
  if (argc == 2 && std::string_view(argv[1]) == MSWindowsClipboardFiles::kPrintFilesArg)
    return MSWindowsClipboardFiles::printFiles();
  if (argc == 2 && std::string_view(argv[1]) == MSWindowsClipboardFiles::kPrintClipboardArg)
    return MSWindowsClipboardFiles::printClipboard();

  ArchMiscWindows::setInstanceWin32(GetModuleHandle(nullptr));
#endif

#if defined(DESKFLOW_CORE_QT_EVENT_LOOP)
  QCoreApplication::setApplicationName(QStringLiteral("%1 Core").arg(kAppName));
  QGuiApplication qtApp(argc, argv);
#endif
#if defined(DESKFLOW_OS_MACOS)
  initCocoaApp();
#endif

  Arch arch;
  arch.init();

  Log log;
#if defined(DESKFLOW_CORE_QT_EVENT_LOOP)
  qInstallMessageHandler(qtMessageHandler);
#endif

  const auto args = deskflow::commandLineArguments(argc, argv);
  CoreArgParser parser(args);

  // Print any parser errors
  if (const auto error = parser.errorText(); !error.empty()) {
    std::printf("%s\n", error.c_str());
  }

  if (parser.help()) {
    std::fputs(parser.helpText().c_str(), stdout);
    return s_exitSuccess;
  }

  if (parser.version()) {
    std::fputs(parser.versionText().c_str(), stdout);
    return s_exitSuccess;
  }

  // Before we check any more args we need to check for a duplicate process.
  // The token goes away with this process, even if it crashes.
  const deskflow::SingleInstance instance(kCoreBinName);
  if (!instance.isFirst() && parser.singleInstanceOnly()) {
    LOG_WARN("an instance of deskflow core is already running");
    return s_exitDuplicate;
  }

  parser.parse();

  EventQueue events;
  const auto processName = deskflow::programName(args.empty() ? std::string(kCoreBinName) : args.front());

  App *coreApp = createApp(parser, events, processName);

  deskflow::core::ipc::CoreIpcServer ipcServer;
  ipcServer.onStopProcessRequested = [coreApp] { coreApp->quit(); };
  ipcServer.listen();

  std::atomic<bool> coreFinished = false;
  std::thread coreThread([coreApp, &coreFinished] {
    LOG_DEBUG("core thread started");
    coreApp->run();
    LOG_DEBUG("core thread finished");
    coreFinished = true;
  });
  runMainThreadUntil(coreFinished);
  coreThread.join();

  const int exitCode = coreApp->getExitCode();

  // its stop handler uses the app; the server itself lives on while the core shuts down
  ipcServer.stop();
  delete coreApp;

  LOG_DEBUG("core exited, code: %d", exitCode);
  return exitCode;
}
