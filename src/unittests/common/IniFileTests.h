/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class IniFileTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void valueConversions();
  void readsWhatQSettingsWrites();
  void qsettingsReadsWhatIniFileWrites();
  void editKeepsTheRestOfTheFile();
  void removeTakesChildKeys();
  void keysAndGroups();

private:
  QTemporaryDir m_dir;
};
