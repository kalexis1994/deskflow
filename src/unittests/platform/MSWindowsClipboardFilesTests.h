/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

//! The CF_HDROP data, without touching the real clipboard
class MSWindowsClipboardFilesTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void pathsRoundTrip();
  void forwardSlashesBecomeBackslashes();
  void noFilesNoData();
  void readsAnsiData();
  void readsNothingFromNothing();
  void dropEffectIsCopy();
  void packedPathsRoundTrip();
  void truncatedPathIsDropped();
  //! By hand: copy a file in Explorer, then run with DESKFLOW_TEST_REAL_CLIPBOARD=1.
  //! Only reads the clipboard.
  void readsWhatExplorerCopied();
  //! By hand, the same, with DESKFLOW_TEST_CORE set to deskflow-core.exe
  void readsThroughTheHelper();
};
