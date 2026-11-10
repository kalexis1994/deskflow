/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class MSWindowsImageTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void roundTrips32BitBottomUp();
  void roundTrips24BitTopDown();
  void makesARealPng();
  void laysTransparencyOverWhite();
  void refusesWhatIsntAnImage();
  void trimsWhatFollowsThePng();
};
