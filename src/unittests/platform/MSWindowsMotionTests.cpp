/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsMotionTests.h"

#include "platform/MSWindowsMotion.h"
#include "platform/MSWindowsMouseCapture.h"

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

namespace {

// two monitors side by side, the left one primary
constexpr long kLeft = 0, kTop = 0, kRight = 3840, kBottom = 1080;

bool captured(long left, long top, long right, long bottom, bool shown)
{
  return isMouseCaptured(left, top, right, bottom, kLeft, kTop, kRight, kBottom, shown);
}

} // namespace

void MSWindowsMotionTests::aGameTakesTheMouse()
{
  // a full-screen game on the first monitor, and one that keeps the cursor in a dot
  QVERIFY(captured(0, 0, 1920, 1080, false));
  QVERIFY(captured(960, 540, 961, 541, false));
  // a game in a window on the second monitor
  QVERIFY(captured(2200, 100, 3600, 900, false));
}

void MSWindowsMotionTests::hiddenOrConfinedAloneIsNotAGame()
{
  // a video player hiding an idle cursor: nothing confines it
  QVERIFY(!captured(kLeft, kTop, kRight, kBottom, false));
  // a strategy game in a window confines a cursor it shows
  QVERIFY(!captured(0, 0, 1920, 1080, true));
  // nothing at all
  QVERIFY(!captured(kLeft, kTop, kRight, kBottom, true));
  // an empty confinement says nothing
  QVERIFY(!captured(500, 500, 500, 500, false));
}

QTEST_MAIN(MSWindowsMotionTests)
