/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTest>

class ClipboardUtilTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void hasContent();
  void sameContentIgnoresLineEndings();
  void sameContentComparesFormats();
  void fitKeepsEverythingUnderTheLimit();
  void fitDropsBitmapThenHtml();
  void fitGivesUpWhenTextIsTooBig();
  void echoGuard();
  void activityJsonDescribesContent();
  void activityJsonPreviewIsOptionalAndShort();

private:
  Log m_log;
};
