/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

//! What the service accepts to install; never runs anything
class MSWindowsUpdateTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void init();
  void parsesVersions();
  void readsTheSignedVersion();
  void copiesANewerSignedInstaller();
  void refusesWhatIsntNewer();
  void refusesAnEditedInstaller();
  void refusesAnotherKey();
  void refusesWithoutSignature();
  void refusesWithoutAnInstalledVersion();
  void startsFromAnEmptyFolderEachTime();

private:
  QTemporaryDir m_dir;
};
