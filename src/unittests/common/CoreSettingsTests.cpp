/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreSettingsTests.h"

#include "common/CoreSettings.h"

#include <QFile>
#include <QSettings>

using S = CoreSettings;

void CoreSettingsTests::init()
{
  QVERIFY(m_dir.isValid());
  m_file = m_dir.filePath(QStringLiteral("%1.conf").arg(QTest::currentTestFunction()));
  QFile::remove(m_file);
  S::reset();
  S::setSettingsFile(m_file.toStdString());
}

void CoreSettingsTests::defaults()
{
  QCOMPARE(S::value(S::Core::Port).toInt(), 24800);
  QVERIFY(S::value(S::Security::TlsEnabled).toBool());
  QVERIFY(S::value(S::Security::CheckPeers).toBool());
  QVERIFY(S::value(S::Core::UseHooks).toBool());
  QVERIFY(!S::value(S::Core::PreventSleep).toBool());
  QCOMPARE(S::logLevelText(), "Info");
  QCOMPARE(S::networkProtocol(), NetworkProtocol::Barrier);
  QCOMPARE(S::value(S::Server::ClipboardSize).toUInt(), 3u);
  QCOMPARE(S::value(S::Server::Heartbeat).toInt(), 5000);
  QCOMPARE(S::value(S::Client::YScrollScale).toDouble(), 1.0);
  QCOMPARE(S::value(S::computerKey("pc", S::Computer::ModifierAlt)).toString(), "alt");
  QVERIFY(!S::value(S::computerKey("pc", S::Computer::HalfDuplexCapsLock)).toBool());
  QCOMPARE(S::value(S::Core::CoreMode).toInt(), static_cast<int>(S::CoreMode::None));
  QVERIFY(!S::value(S::Core::ComputerName).toString().empty());

  // reading never creates the file
  QVERIFY(!QFile::exists(m_file));
}

void CoreSettingsTests::readsGuiValues()
{
  {
    QSettings gui(m_file, QSettings::IniFormat);
    gui.setValue(QStringLiteral("core/port"), 24900);
    gui.setValue(QStringLiteral("core/coreMode"), 2);
    gui.setValue(QStringLiteral("security/tlsEnabled"), false);
    gui.setValue(QStringLiteral("server/protocol"), QStringLiteral("Synergy"));
    gui.setValue(QStringLiteral("client/yScrollScale"), 2.5);
    gui.setValue(QStringLiteral("log/level"), QStringLiteral("DEBUG"));
  }

  QCOMPARE(S::value(S::Core::Port).toInt(), 24900);
  QCOMPARE(S::value(S::Core::CoreMode).toInt(), static_cast<int>(S::CoreMode::Server));
  QVERIFY(!S::value(S::Security::TlsEnabled).toBool());
  QCOMPARE(S::networkProtocol(), NetworkProtocol::Synergy);
  QCOMPARE(S::value(S::Client::YScrollScale).toDouble(), 2.5);
  QCOMPARE(S::logLevelText(), "DEBUG");
}

void CoreSettingsTests::seesChangesOnDisk()
{
  {
    QSettings gui(m_file, QSettings::IniFormat);
    gui.setValue(QStringLiteral("client/remoteHost"), QStringLiteral("10.0.0.1"));
  }
  QCOMPARE(S::value(S::Client::RemoteHost).toString(), "10.0.0.1");

  {
    QSettings gui(m_file, QSettings::IniFormat);
    gui.setValue(QStringLiteral("client/remoteHost"), QStringLiteral("192.168.1.60"));
  }
  QCOMPARE(S::value(S::Client::RemoteHost).toString(), "192.168.1.60");
}

void CoreSettingsTests::setValueWritesForQSettings()
{
  {
    QSettings gui(m_file, QSettings::IniFormat);
    gui.setValue(QStringLiteral("core/computerName"), QStringLiteral("kept"));
  }

  S::setValue(S::Daemon::LogLevel, "DEBUG");
  S::setValue(S::Daemon::ConfigFile, "C:/Users/me/AppData/Roaming/Deskflow/Deskflow.conf");
  S::setValue(S::Core::PreventSleep, true);
  QCOMPARE(S::value(S::Daemon::LogLevel).toString(), "DEBUG");

  {
    QSettings gui(m_file, QSettings::IniFormat);
    QCOMPARE(gui.value(QStringLiteral("daemon/logLevel")).toString(), QStringLiteral("DEBUG"));
    QCOMPARE(
        gui.value(QStringLiteral("daemon/configFile")).toString(),
        QStringLiteral("C:/Users/me/AppData/Roaming/Deskflow/Deskflow.conf")
    );
    QVERIFY(gui.value(QStringLiteral("core/preventSleep")).toBool());
    QCOMPARE(gui.value(QStringLiteral("core/computerName")).toString(), QStringLiteral("kept"));
  }

  // an invalid value removes the key, as the daemon does when settings are cleared
  S::setValue(S::Daemon::ConfigFile);
  QVERIFY(S::value(S::Daemon::ConfigFile).toString().empty());
  QSettings gui(m_file, QSettings::IniFormat);
  QVERIFY(!gui.contains(QStringLiteral("daemon/configFile")));
  QVERIFY(gui.contains(QStringLiteral("daemon/logLevel")));

  // the computer name is cleaned the way the GUI cleans it
  S::setValue(S::Core::ComputerName, " my  pc! ");
  QCOMPARE(S::value(S::Core::ComputerName).toString(), "my_pc");
}

void CoreSettingsTests::knownComputers()
{
  {
    QSettings gui(m_file, QSettings::IniFormat);
    gui.setValue(QStringLiteral("computer_alexis-server/name"), QStringLiteral("alexis-server"));
    gui.setValue(QStringLiteral("computer_alexis-server/aliases"), QStringList{"a1", "a2"});
    gui.setValue(QStringLiteral("computer_laptop/name"), QStringLiteral("laptop"));
    gui.setValue(QStringLiteral("computer_laptop/modifierAlt"), QStringLiteral("ctrl"));
    gui.setValue(QStringLiteral("core/port"), 24800);
  }

  auto computers = S::knownComputers();
  std::sort(computers.begin(), computers.end());
  QVERIFY(computers == (std::vector<std::string>{"alexis-server", "laptop"}));
  QVERIFY(
      S::value(S::computerKey("alexis-server", S::Computer::Aliases)).toStringList() ==
      (std::vector<std::string>{"a1", "a2"})
  );
  QCOMPARE(S::value(S::computerKey("laptop", S::Computer::ModifierAlt)).toString(), "ctrl");
  QCOMPARE(S::value(S::computerKey("laptop", S::Computer::ModifierCtrl)).toString(), "ctrl");
}

void CoreSettingsTests::serverConfigFile()
{
  QVERIFY(QString::fromStdString(S::serverConfigFile()).endsWith(QStringLiteral("/deskflow-server.conf")));

  {
    QSettings gui(m_file, QSettings::IniFormat);
    gui.setValue(QStringLiteral("server/externalConfig"), true);
    gui.setValue(QStringLiteral("server/externalConfigFile"), QStringLiteral("D:/configs/my server.conf"));
  }
  QCOMPARE(S::serverConfigFile(), "D:/configs/my server.conf");
}

void CoreSettingsTests::tlsPaths()
{
  const auto tlsDir = QString::fromStdString(S::tlsDir());
  QVERIFY(tlsDir.endsWith(QStringLiteral("/tls")));
  QCOMPARE(QString::fromStdString(S::tlsTrustedServersDb()), tlsDir + QStringLiteral("/trusted-servers"));
  QCOMPARE(QString::fromStdString(S::tlsTrustedClientsDb()), tlsDir + QStringLiteral("/trusted-clients"));
  QCOMPARE(QString::fromStdString(S::value(S::Security::Certificate).toString()), tlsDir + "/deskflow.pem");
}

void CoreSettingsTests::cleanComputerName()
{
  QCOMPARE(S::cleanComputerName("  my   computer  "), "my_computer");
  QCOMPARE(S::cleanComputerName("-_.name._-"), "name");
  QCOMPARE(S::cleanComputerName("na$me (2)"), "name_2");
  QCOMPARE(S::cleanComputerName("Ñandú"), "and");
  QCOMPARE(S::cleanComputerName(std::string(300, 'a')).size(), size_t(255));
}

QTEST_MAIN(CoreSettingsTests)
