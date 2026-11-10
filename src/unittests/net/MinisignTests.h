/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

//! Signatures made by `tauri signer` with a throwaway key
class MinisignTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void acceptsWhatTauriSigned();
  void acceptsTheTextItself();
  void rejectsOtherData();
  void rejectsAnEditedTrustedComment();
  void rejectsAnotherKey();
  void rejectsGarbage();
  void decodesBase64();
};
