/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsMotionTests.h"

#include "platform/MSWindowsMotion.h"

namespace {

// one 1920x1080 monitor, the cursor kept at its center
bool preWarp(int32_t dx, int32_t dy)
{
  return isPreWarpMotion(dx, dy, 0, 0, 1920, 1080, 960, 540);
}

} // namespace

void MSWindowsMotionTests::realMotionsPass()
{
  QVERIFY(!preWarp(0, 0));
  QVERIFY(!preWarp(1, -1));
  QVERIFY(!preWarp(-120, 80));
  // a very fast flick
  QVERIFY(!preWarp(250, 0));
  QVERIFY(!preWarp(480, 270));
  QVERIFY(!preWarp(-480, -270));
}

void MSWindowsMotionTests::positionsBeforeTheWarpAreDropped()
{
  // at the edge the cursor crossed
  QVERIFY(preWarp(959, 0));
  QVERIFY(preWarp(-960, 0));
  QVERIFY(preWarp(0, 539));
  QVERIFY(preWarp(0, -540));
}

void MSWindowsMotionTests::fastCursorBehindTheEdgeIsDropped()
{
  // position 1890: the cursor stopped 30 pixels short of the edge when the event that
  // reached the jump zone was eaten. A margin of 10 pixels let this through, and the
  // cursor landed in the middle of the other computer.
  QVERIFY(preWarp(1890 - 960, 0));
  QVERIFY(preWarp(100 - 960, 0));
  QVERIFY(preWarp(0, 1000 - 540));
  // well short of it, in a flick
  QVERIFY(preWarp(1500 - 960, 0));
}

void MSWindowsMotionTests::monitorsOffCenter()
{
  // a second monitor to the left: the desktop goes from -1920, the center is on the right one
  const auto twoMonitors = [](int32_t dx) { return isPreWarpMotion(dx, 0, -1920, 0, 3840, 1080, 960, 540); };
  QVERIFY(!twoMonitors(400));
  QVERIFY(!twoMonitors(-1000));
  QVERIFY(twoMonitors(900));
  QVERIFY(twoMonitors(-2850));
}

QTEST_MAIN(MSWindowsMotionTests)
