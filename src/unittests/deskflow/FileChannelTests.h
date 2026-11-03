/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class FileChannelTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void init();
  void cleanup();

  // FileChannel
  void copyArrivesWhole();
  void neverMoreThanTheWindowInFlight();
  void copyOfOnlyFolders();
  void receiverRefuses();
  void damageStopsBothSides();
  void senderCancels();
  void receiverCancels();
  void newerCopyReplacesOlder();
  void unknownCopyIgnored();
  void tooLargeForTheOtherComputer();
  void sourceGoneStopsTheCopy();
  void malformedMessagesStopTheCopy();
  void disconnectDeletesPartialCopy();
  void noUserNoCopy();

  void progressGoesUpToTheTotal();

  // the protocol's messages
  void messagesCarryAnyBytes();

  // what the GUI is told
  void activityJsonSummarizesTheCopy();
  void reasonsBecomeCodes();
  void progressJson();

  // FileHub
  void serverCopyGoesToEnteredClient();
  void sameServerCopyIsNotSentAgain();
  void clientCopyComesThroughTheServer();
  void newerCopyStopsTransfers();
  void cursorMovingOnRedirectsTheTransfer();
  void clientsThatCantReceiveAreSkipped();
  void failedCopyIsNotRetried();
  void lateCopyIsDropped();
  void disconnectForgetsTheClient();

private:
  Log m_log;
  QTemporaryDir *m_dir = nullptr;
};
