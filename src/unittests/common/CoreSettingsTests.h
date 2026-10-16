/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class CoreSettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void init();
  void defaults();
  void readsGuiValues();
  void seesChangesOnDisk();
  void setValueWritesForQSettings();
  void knownComputers();
  void serverConfigFile();
  void tlsPaths();
  void cleanComputerName();

private:
  QString m_file;
  QTemporaryDir m_dir;
};
