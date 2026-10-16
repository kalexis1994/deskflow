/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/ILogOutputter.h"

#include <QDebug>

namespace deskflow::gui {

//! Sends Deskflow log messages (LOG_*) to Qt's message handler
/*!
Code shared with the core (settings, fingerprints, platform helpers) logs through
Deskflow's Log. In the GUI those messages go on to Qt's handler, which shows them in
the log dock as before. Errors map to warnings: the GUI's handler opens an error
dialog for critical messages, which shared code never asked for.
*/
class QtLogOutputter : public ILogOutputter
{
public:
  void open(const std::string &) override
  {
    // do nothing
  }

  void close() override
  {
    // do nothing
  }

  bool write(LogLevel::Level level, const std::string &message) override
  {
    const auto text = QString::fromStdString(message).trimmed();
    switch (level) {
      using enum LogLevel::Level;
    case Fatal:
    case Error:
    case Warning:
      qWarning().noquote() << text;
      break;
    case Debug:
    case Verbose:
      qDebug().noquote() << text;
      break;
    default:
      qInfo().noquote() << text;
      break;
    }
    return true;
  }
};

} // namespace deskflow::gui
