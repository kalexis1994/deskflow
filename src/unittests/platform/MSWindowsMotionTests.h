/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class MSWindowsMotionTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void realMotionsPass();
  void positionsBeforeTheWarpAreDropped();
  void fastCursorBehindTheEdgeIsDropped();
  void monitorsOffCenter();
};
