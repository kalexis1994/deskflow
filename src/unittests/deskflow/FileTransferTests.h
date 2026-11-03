/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class FileTransferTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void init();
  void cleanup();

  // FileList
  void pathsAccepted();
  void pathsRejected();
  void listRules();
  void listLimits();
  void serializeRoundTrip();
  void deserializeRejectsMalformed();
  void collectWalksFoldersInOrder();
  void collectSkipsLinks();
  void collectEnforcesLimits();
  void collectRefusesADrive();

  // Reader and Writer
  void sha256();
  void roundTrip();
  void roundTripInOneChunk();
  void writerRejectsDamage();
  void writerRejectsDisorder();
  void writerRejectsShortFile();
  void writerRejectsBadIdAndList();
  void writerNeedsDiskSpace();
  void writerDeletesUnfinishedCopy();
  void writerRefusesCaseClash();
  void writerRefusesLongPaths();
  void readerDetectsChangeBeforeReading();
  void readerDetectsChangeWhileReading();

  // Cache
  void newIds();
  void cleanupKeepsNewestAndOnlyItsOwn();
  void userCacheRoot();

private:
  QTemporaryDir *m_dir = nullptr;
};
