/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorsTests.h"

#include "deskflow/Monitors.h"

using deskflow::Monitor;

void MonitorsTests::roundTrips()
{
  // a 1080p monitor and a taller one to its left, as Windows numbers them
  const std::vector<Monitor> monitors = {{0, 0, 1920, 1080}, {-1440, -300, 1440, 2560}};
  const auto text = deskflow::formatMonitors(monitors);
  QCOMPARE(text, std::string("0,0,1920,1080;-1440,-300,1440,2560"));
  QVERIFY(deskflow::parseMonitors(text) == monitors);
  QVERIFY(deskflow::parseMonitors("").empty());
}

void MonitorsTests::leavesOutWhatIsntAMonitor()
{
  const std::vector<Monitor> one = {{0, 0, 800, 600}};
  QVERIFY(deskflow::parseMonitors("0,0,800,600;0,0,0,600") == one);   // no width
  QVERIFY(deskflow::parseMonitors("0,0,800,600;1,2,3") == one);       // three numbers
  QVERIFY(deskflow::parseMonitors("0,0,800,600;1,2,3,4,5") == one);   // five
  QVERIFY(deskflow::parseMonitors("0,0,800,600;a,0,800,600") == one); // not a number
  QVERIFY(deskflow::parseMonitors("0,0,800,600;0,0,999999,600") == one);
  QVERIFY(deskflow::parseMonitors("0,0,800,600;") == one);
}

void MonitorsTests::keepsAtMostSixteen()
{
  std::string text;
  for (int i = 0; i < 40; ++i)
    text += std::to_string(i * 100) + ",0,100,100;";
  QCOMPARE(deskflow::parseMonitors(text).size(), deskflow::kMaxMonitors);
}

void MonitorsTests::describesEveryComputerForTheApp()
{
  const std::map<std::string, std::vector<Monitor>> computers = {
      {"escritorio", {{0, 0, 1920, 1080}, {1920, 0, 2560, 1440}}},
      {"lap\"top", {}},
  };
  QCOMPARE(
      deskflow::monitorsJson(computers),
      std::string("{\"escritorio\":[[0,0,1920,1080],[1920,0,2560,1440]],\"lap\\\"top\":[]}")
  );
  QCOMPARE(deskflow::monitorsJson({}), std::string("{}"));
}

QTEST_MAIN(MonitorsTests)
