/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcServerTests.h"

#include "common/VersionInfo.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "deskflow/ipc/DaemonIpcServer.h"

#include <QCoreApplication>
#include <QLocalSocket>

#if defined(_WIN32)
#include "arch/Utf8.h"
#include <aclapi.h>
#include <sddl.h>
#endif

#include <atomic>
#include <mutex>
#include <thread>

using namespace deskflow::core::ipc;

namespace {

// Never the real "deskflow-core"/"deskflow-daemon": those may belong to a running Deskflow.
std::string testName(const char *suffix)
{
  return "deskflow-ipc-test-" + std::to_string(QCoreApplication::applicationPid()) + "-" + suffix;
}

QString versionId()
{
  return QStringLiteral("%1+%2").arg(kVersion, kVersionGitSha);
}

//! The Qt GUI's side of the connection: QLocalSocket, as gui/ipc/IpcClient uses it
class Client
{
public:
  bool connect(const std::string &name)
  {
    m_socket.connectToServer(QString::fromStdString(name));
    return m_socket.waitForConnected(3000);
  }

  void send(const QByteArray &data)
  {
    m_socket.write(data);
    m_socket.flush();
    m_socket.waitForBytesWritten(1000);
  }

  QString readLine(int timeout = 3000)
  {
    while (!m_socket.canReadLine()) {
      if (!m_socket.waitForReadyRead(timeout))
        return QStringLiteral("<nothing>");
    }
    return QString::fromUtf8(m_socket.readLine()).trimmed();
  }

  bool hello()
  {
    send("hello=" + versionId().toUtf8() + "\n");
    return readLine() == QStringLiteral("hello=") + versionId();
  }

  bool waitForDisconnected()
  {
    return m_socket.state() == QLocalSocket::UnconnectedState || m_socket.waitForDisconnected(3000);
  }

  void close()
  {
    m_socket.disconnectFromServer();
  }

private:
  QLocalSocket m_socket;
};

} // namespace

void IpcServerTests::helloReplaysQueuedMessages()
{
  CoreIpcServer server(testName("replay"));
  server.listen();

  // sent before any GUI connects, so queued
  ipcSendConnectionState(deskflow::core::ConnectionState::Connecting);
  ipcSendToClient("secureSocket", "TLSv1.3");

  Client client;
  QVERIFY(client.connect(testName("replay")));
  QVERIFY(client.hello());
  QCOMPARE(client.readLine(), QStringLiteral("connectionState=Connecting"));
  QCOMPARE(client.readLine(), QStringLiteral("secureSocket=TLSv1.3"));
}

void IpcServerTests::versionMismatch()
{
  CoreIpcServer server(testName("mismatch"));
  server.listen();

  Client client;
  QVERIFY(client.connect(testName("mismatch")));
  client.send("hello=0.0.0+old\n");
  QCOMPARE(client.readLine(), QStringLiteral("versionMismatch=") + versionId());

  // a mismatched client still gets answers and broadcasts
  client.send("noop\n");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
}

void IpcServerTests::helloWithoutVersionDisconnects()
{
  CoreIpcServer server(testName("noversion"));
  server.listen();

  Client client;
  QVERIFY(client.connect(testName("noversion")));
  client.send("hello\n");
  QCOMPARE(client.readLine(), QStringLiteral("error=missing version"));
  QVERIFY(client.waitForDisconnected());
}

void IpcServerTests::messagesSplitAcrossWrites()
{
  CoreIpcServer server(testName("split"));
  server.listen();

  Client client;
  QVERIFY(client.connect(testName("split")));
  client.send("no");
  QTest::qWait(50);
  client.send("op\nnoop\r\nno");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
  client.send("op\n");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
}

void IpcServerTests::daemonCommands()
{
  DaemonIpcServer server("C:/ProgramData/Deskflow/deskflow-daemon.log", testName("daemon"));

  std::mutex mutex;
  std::string logLevel;
  std::string configFile;
  std::atomic<int> starts = 0;
  std::atomic<int> stops = 0;
  std::atomic<int> clears = 0;
  server.onLogLevelChanged = [&](const std::string &value) {
    std::scoped_lock lock{mutex};
    logLevel = value;
  };
  server.onConfigFileChanged = [&](const std::string &value) {
    std::scoped_lock lock{mutex};
    configFile = value;
  };
  server.onStartProcessRequested = [&] { ++starts; };
  server.onStopProcessRequested = [&] { ++stops; };
  server.onClearSettingsRequested = [&] { ++clears; };
  server.listen();

  Client client;
  QVERIFY(client.connect(testName("daemon")));
  QVERIFY(client.hello());

  client.send("logLevel=DEBUG\n");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));

  // the argument is everything after the first '='
  client.send("configFile=C:/Users/Ñandú/a=b/Deskflow.conf\n");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));

  client.send("logPath\n");
  QCOMPARE(client.readLine(), QStringLiteral("logPath=C:/ProgramData/Deskflow/deskflow-daemon.log"));

  client.send("start\nstop\nclearSettings\n");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
  QCOMPARE(client.readLine(), QStringLiteral("ok"));

  client.send("logLevel=\n");
  QCOMPARE(client.readLine(), QStringLiteral("error"));

  std::scoped_lock lock{mutex};
  QCOMPARE(logLevel, "DEBUG");
  QCOMPARE(configFile, "C:/Users/Ñandú/a=b/Deskflow.conf");
  QCOMPARE(starts.load(), 1);
  QCOMPARE(stops.load(), 1);
  QCOMPARE(clears.load(), 1);
}

void IpcServerTests::coreStop()
{
  CoreIpcServer server(testName("stop"));
  std::atomic<bool> stopRequested = false;
  server.onStopProcessRequested = [&] { stopRequested = true; };
  server.listen();

  Client client;
  QVERIFY(client.connect(testName("stop")));
  QVERIFY(client.hello());
  client.send("stop\n");
  QCOMPARE(client.readLine(), QStringLiteral("ok"));
  QCOMPARE(client.readLine(), QStringLiteral("bye"));
  QTRY_VERIFY(stopRequested.load());
}

void IpcServerTests::broadcastReachesEveryClient()
{
  CoreIpcServer server(testName("broadcast"));
  server.listen();

  Client first;
  Client second;
  QVERIFY(first.connect(testName("broadcast")));
  QVERIFY(second.connect(testName("broadcast")));
  QVERIFY(first.hello());
  QVERIFY(second.hello());

  // from another thread, as the core's threads do
  std::thread([] { ipcSendToClient("connectedClients", "laptop,desktop"); }).join();
  QCOMPARE(first.readLine(), QStringLiteral("connectedClients=laptop,desktop"));
  QCOMPARE(second.readLine(), QStringLiteral("connectedClients=laptop,desktop"));
}

void IpcServerTests::disconnectedClientsStopReceiving()
{
  CoreIpcServer server(testName("gone"));
  server.listen();

  {
    Client client;
    QVERIFY(client.connect(testName("gone")));
    QVERIFY(client.hello());
    client.close();
  }
  QTest::qWait(200); // the server notices the disconnect on its own thread

  // nobody is connected any more, so this waits for the next GUI
  ipcSendToClient("retryIn", "5");

  Client next;
  QVERIFY(next.connect(testName("gone")));
  QVERIFY(next.hello());
  QCOMPARE(next.readLine(), QStringLiteral("retryIn=5"));
}

void IpcServerTests::sendWithoutServer()
{
  QVERIFY(!CoreIpcServer::exists());
  ipcSendToClient("ignored"); // e.g. shared code running in the GUI
}

void IpcServerTests::latestOnlyKeepsOneQueued()
{
  CoreIpcServer server(testName("latest"));
  server.listen();

  // clipboard activity is state: a GUI that connects later wants the last one only
  ipcSendToClient("clipboard", "{\"event\":\"sent\",\"n\":1}", true);
  ipcSendToClient("connectionState", "Connecting");
  ipcSendToClient("clipboard", "{\"event\":\"sent\",\"n\":2}", true);

  Client client;
  QVERIFY(client.connect(testName("latest")));
  QVERIFY(client.hello());
  QCOMPARE(client.readLine(), QStringLiteral("connectionState=Connecting"));
  QCOMPARE(client.readLine(), QStringLiteral("clipboard={\"event\":\"sent\",\"n\":2}"));
  QCOMPARE(client.readLine(200), QStringLiteral("<nothing>"));
}

void IpcServerTests::pipePermissions()
{
#if defined(_WIN32)
  CoreIpcServer server(testName("acl"));
  server.listen();

  const auto path = deskflow::utf8ToWide(LocalServer::endpoint(testName("acl")));
  HANDLE pipe = CreateFileW(path.c_str(), READ_CONTROL, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  QVERIFY(pipe != INVALID_HANDLE_VALUE);

  PSECURITY_DESCRIPTOR descriptor = nullptr;
  QCOMPARE(
      GetSecurityInfo(
          pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, &descriptor
      ),
      DWORD(ERROR_SUCCESS)
  );
  LPWSTR sddl = nullptr;
  QVERIFY(ConvertSecurityDescriptorToStringSecurityDescriptorW(
      descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &sddl, nullptr
  ));
  const auto text = QString::fromWCharArray(sddl);
  LocalFree(sddl);
  LocalFree(descriptor);
  CloseHandle(pipe);

  // the GUI (a user) must be able to read and write the daemon's pipe (SYSTEM)...
  QVERIFY2(text.contains(QStringLiteral(";0x12019f;;;WD)")), qPrintable(text));
  // ...but not take it over by changing its permissions or owner, as QLocalServer allowed
  QVERIFY2(
      !text.contains(QStringLiteral(";FA;;;WD)")) && !text.contains(QStringLiteral(";GA;;;WD)")), qPrintable(text)
  );
  QVERIFY2(text.contains(QStringLiteral(";FA;;;SY)")) || text.contains(QStringLiteral(";GA;;;SY)")), qPrintable(text));
#else
  QSKIP("named pipe permissions are Windows only; the Unix socket is chmod 0777 like QLocalServer's");
#endif
}

QTEST_MAIN(IpcServerTests)
