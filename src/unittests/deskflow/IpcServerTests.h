/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTest>

class IpcServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void helloReplaysQueuedMessages();
  void versionMismatch();
  void helloWithoutVersionDisconnects();
  void messagesSplitAcrossWrites();
  void daemonCommands();
  void coreStop();
  void broadcastReachesEveryClient();
  void disconnectedClientsStopReceiving();
  void sendWithoutServer();
  void pipePermissions();

private:
  Log m_log;
};
