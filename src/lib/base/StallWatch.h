/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <stop_token>

//! Says in the log when the main loop spends too long on one event, and on what.
//!
//! A client once stopped reading the server for 9 seconds right after taking the
//! clipboard, five connections in a row, and the log couldn't tell whether the loop was
//! stuck in a call or simply had nothing to read.
namespace deskflow::stall {

//! The main loop is handling an event (until done() is called)
void busy();
//! The main loop finished the event and waits for the next one
void done();

//! What the main loop is doing right now, for the report; nests, and restores the
//! outer step when it goes. \p what must be a string literal.
class Step
{
public:
  explicit Step(const char *what);
  ~Step();
  Step(const Step &) = delete;
  Step &operator=(const Step &) = delete;

private:
  const char *m_outer;
};

//! Checks the main loop until \p stop is requested (on a thread of its own)
void watch(const std::stop_token &stop);

} // namespace deskflow::stall
