/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreArgParserTests.h"

#include "CoreArgParser.h"
#include "common/CoreSettings.h"
#include "deskflow/ProcessUtils.h"

#include <QCoreApplication>

using Args = std::vector<std::string>;

namespace {

CoreArgParser parsed(const Args &args)
{
  CoreArgParser parser(args);
  parser.parse();
  return parser;
}

} // namespace

void CoreArgParserTests::modes()
{
  QVERIFY(parsed({"deskflow-core", "server"}).serverMode());
  QVERIFY(parsed({"deskflow-core", "client"}).clientMode());
  QVERIFY(parsed({"deskflow-core", "Client"}).clientMode()); // case doesn't matter
  QVERIFY(!parsed({"deskflow-core", "client"}).serverMode());
}

void CoreArgParserTests::settingsOptionForms()
{
  // the daemon and the GUIs start the core as: deskflow-core server --settings "<file>"
  const Args forms[] = {
      {"deskflow-core", "server", "--settings", "C:/Users/Ñandú/a b/Deskflow.conf"},
      {"deskflow-core", "server", "--settings=C:/Users/Ñandú/a b/Deskflow.conf"},
      {"deskflow-core", "server", "-s", "C:/Users/Ñandú/a b/Deskflow.conf"},
      {"deskflow-core", "-settings", "C:/Users/Ñandú/a b/Deskflow.conf", "server"}, // options before the mode
  };
  for (const auto &args : forms) {
    CoreSettings::reset();
    const auto parser = parsed(args);
    QVERIFY(parser.errorText().empty());
    QVERIFY(parser.serverMode());
    QVERIFY2(
        CoreSettings::settingsFile().ends_with("/Users/Ñandú/a b/Deskflow.conf"), CoreSettings::settingsFile().c_str()
    );
  }
  CoreSettings::reset();
}

void CoreArgParserTests::flags()
{
  QVERIFY(CoreArgParser({"deskflow-core", "--help"}).help());
  QVERIFY(CoreArgParser({"deskflow-core", "-h"}).help());
  QVERIFY(CoreArgParser({"deskflow-core", "-v"}).version());
  QVERIFY(CoreArgParser({"deskflow-core", "--version"}).version());
  QVERIFY(CoreArgParser({"deskflow-core", "server"}).singleInstanceOnly());
  QVERIFY(!CoreArgParser({"deskflow-core", "server", "--new-instance"}).singleInstanceOnly());

  // after "--" everything is positional
  const auto parser = parsed({"deskflow-core", "--", "client", "--help"});
  QVERIFY(parser.clientMode());
  QVERIFY(!parser.help());
}

void CoreArgParserTests::errors()
{
  QCOMPARE(CoreArgParser({"deskflow-core", "server", "--bogus"}).errorText(), "Unknown option 'bogus'.");
  QCOMPARE(CoreArgParser({"deskflow-core", "server", "-s"}).errorText(), "Missing value after '-s'.");
  QCOMPARE(CoreArgParser({"deskflow-core", "server", "--help=yes"}).errorText(), "Unexpected value after '--help'.");
  QVERIFY(CoreArgParser({"deskflow-core", "server"}).errorText().empty());
}

void CoreArgParserTests::helpAndVersionText()
{
  const CoreArgParser parser({"deskflow-core"});
  const auto help = parser.helpText();
  // the layout QCommandLineParser produced
  QVERIFY2(
      help.find("  -s, --settings <configFile>  override configuration file to use\n") != std::string::npos,
      help.c_str()
  );
  QVERIFY2(
      help.find("  --new-instance               Skip the check for a running instance, always\n"
                "                               makes a new instance\n") != std::string::npos,
      help.c_str()
  );
  QVERIFY(
      help.find("  coremode                     The mode to start in either: server or client\n") != std::string::npos
  );

  // the Tauri app reads the version from this line
  const auto version = parser.versionText();
  QVERIFY2(
      version.find(" v") != std::string::npos && version.find(", protocol v") != std::string::npos, version.c_str()
  );
}

void CoreArgParserTests::singleInstance()
{
  const auto name = "deskflow-instance-test-" + std::to_string(QCoreApplication::applicationPid());
  {
    const deskflow::SingleInstance first(name);
    QVERIFY(first.isFirst());
    const deskflow::SingleInstance second(name);
    QVERIFY(!second.isFirst());
  }
  // released with its owner
  const deskflow::SingleInstance again(name);
  QVERIFY(again.isFirst());
}

QTEST_MAIN(CoreArgParserTests)
