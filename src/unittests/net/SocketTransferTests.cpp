/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// A large burst over a real connection must arrive whole. A screenshot on the server's
// clipboard (tens of MB, written at once) once stopped everything the server sent to a
// client after it: keepalives too, until the client gave the server up for dead.

#include "arch/Arch.h"
#include "arch/Utf8.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "common/CoreSettings.h"
#include "net/FingerprintDatabase.h"
#include "net/NetworkAddress.h"
#include "net/SecureListenSocket.h"
#include "net/SecureSocket.h"
#include "net/SecureUtils.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPListenSocket.h"
#include "net/TCPSocket.h"

#include <openssl/pem.h>
#include <openssl/x509.h>

#include <QTest>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// The core's IPC lives in the deskflow library; nothing listens here
void ipcSendToClient(const std::string &, const std::string &, bool)
{
  // nothing to tell
}

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kPort = 38917;
//! what a 4K screenshot is on the clipboard, uncompressed
constexpr std::size_t kBurstBytes = 40 * 1024 * 1024;
constexpr std::size_t kPieceBytes = 512 * 1024;
//! nothing at all arriving for this long is a stall
constexpr auto kStall = std::chrono::seconds(5);

//! The event loop, which drives the TLS handshake, on a thread of its own
class LoopThread
{
public:
  explicit LoopThread(EventQueue &events) : m_events(events), m_thread([this] { m_events.loop(); })
  {
    m_events.waitForReady();
  }
  ~LoopThread()
  {
    m_events.addEvent(Event(EventTypes::Quit));
    m_thread.join();
  }
  LoopThread(const LoopThread &) = delete;
  LoopThread &operator=(const LoopThread &) = delete;

private:
  EventQueue &m_events;
  std::thread m_thread;
};

std::string portableSettingsDir()
{
  return CoreSettings::portableSettingsFile().substr(0, CoreSettings::portableSettingsFile().find_last_of("/\\"));
}

} // namespace

class SocketTransferTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    m_arch.init();
    m_log.setFilter(LogLevel::Level::Info);
    // Portable mode keeps the settings, certificate and trusted servers next to this
    // program, away from the real ones (CoreSettings::settingsPath)
    const auto dir = portableSettingsDir();
    std::filesystem::create_directories(deskflow::utf8Path(dir + "/tls"));
    std::ofstream(deskflow::utf8Path(CoreSettings::portableSettingsFile())) << "";
    CoreSettings::setSettingsFile(CoreSettings::portableSettingsFile());
    QVERIFY(CoreSettings::isPortableMode());

    const auto pem = dir + "/tls/test.pem";
    deskflow::generatePemSelfSignedCert(pem, 2048);
    CoreSettings::setValue(CoreSettings::Security::Certificate, pem);

    FILE *file = std::fopen(pem.c_str(), "rb");
    QVERIFY(file != nullptr);
    X509 *cert = PEM_read_X509(file, nullptr, nullptr, nullptr);
    std::fclose(file);
    QVERIFY(cert != nullptr);
    const auto fingerprint = deskflow::sslCertFingerprint(cert, Fingerprint::Type::Sha256);
    X509_free(cert);
    FingerprintDatabase db;
    db.addTrusted(fingerprint);
    QVERIFY(db.write(CoreSettings::tlsTrustedServersDb()));
  }

  void cleanupTestCase()
  {
    std::error_code ec;
    std::filesystem::remove_all(deskflow::utf8Path(portableSettingsDir()), ec);
  }

  void plainBurstArrives()
  {
    EventQueue events;
    LoopThread loop(events);
    SocketMultiplexer multiplexer;
    TCPListenSocket listener(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
    TCPSocket client(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
    burst(events, listener, client, kPort);
  }

  void secureBurstArrives()
  {
    EventQueue events;
    LoopThread loop(events);
    SocketMultiplexer multiplexer;
    SecureListenSocket listener(&events, &multiplexer, IArchNetwork::AddressFamily::INet, SecurityLevel::Encrypted);
    SecureSocket client(&events, &multiplexer, IArchNetwork::AddressFamily::INet, SecurityLevel::Encrypted);
    client.initSsl(false); // as the socket factory does
    burst(events, listener, client, kPort + 1);
  }

private:
  void burst(EventQueue &events, IListenSocket &listener, IDataSocket &client, int port)
  {
    // accepted as the server does: on the loop, when the listener says one is waiting
    std::promise<std::unique_ptr<IDataSocket>> accepted;
    auto acceptedFuture = accepted.get_future();
    bool once = false;
    events.addHandler(EventTypes::ListenSocketConnecting, listener.getEventTarget(), [&](const auto &) {
      if (!once) {
        once = true;
        accepted.set_value(listener.accept());
      }
    });

    NetworkAddress address("127.0.0.1", port);
    address.resolve();
    listener.bind(address);
    client.connect(address);

    QVERIFY(acceptedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    std::unique_ptr<IDataSocket> server = acceptedFuture.get();
    events.removeHandler(EventTypes::ListenSocketConnecting, listener.getEventTarget());
    QVERIFY(server != nullptr);
    // the server writes once the handshake is done (ClientListenerAccepted)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // a small message first: the connection works
    {
      const char ping[4] = {'p', 'i', 'n', 'g'};
      server->write(ping, sizeof(ping));
      char got[4] = {};
      uint32_t have = 0;
      const auto by = Clock::now() + std::chrono::seconds(5);
      while (have < sizeof(got) && Clock::now() < by) {
        have += client.read(got + have, sizeof(got) - have);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      QCOMPARE(have, uint32_t{4});
    }

    // as the server does: the whole image queued at once, then small messages (motion,
    // keepalives) now and then while it goes out
    std::vector<char> piece(kPieceBytes, 'x');
    for (std::size_t sent = 0; sent < kBurstBytes; sent += kPieceBytes)
      server->write(piece.data(), static_cast<uint32_t>(piece.size()));

    constexpr std::size_t kSmall = 16;
    const char small[kSmall] = {};
    std::size_t expected = kBurstBytes;
    std::size_t received = 0;
    std::vector<char> buffer(64 * 1024);
    auto lastProgress = Clock::now();
    auto nextSmall = Clock::now();
    const auto giveUp = Clock::now() + std::chrono::seconds(60);
    while (received < expected) {
      if (Clock::now() >= nextSmall && Clock::now() - lastProgress < kStall) {
        server->write(small, kSmall);
        expected += kSmall;
        nextSmall = Clock::now() + std::chrono::milliseconds(20);
      }
      const auto n = client.read(buffer.data(), static_cast<uint32_t>(buffer.size()));
      if (n > 0) {
        received += n;
        lastProgress = Clock::now();
        continue;
      }
      if (Clock::now() - lastProgress > kStall || Clock::now() > giveUp) {
        QFAIL(qPrintable(QStringLiteral("stalled: %1 of %2 bytes arrived").arg(received).arg(expected)));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QCOMPARE(received, expected);
  }

  Arch m_arch;
  Log m_log;
};

QTEST_GUILESS_MAIN(SocketTransferTests)
#include "SocketTransferTests.moc"
