/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/StallWatch.h"

#include "base/Log.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace deskflow::stall {

namespace {

using Clock = std::chrono::steady_clock;

//! one event longer than this is worth a line in the log
constexpr auto kTooLong = std::chrono::seconds(2);
constexpr auto kCheckEvery = std::chrono::milliseconds(250);

//! when the event being handled started, in clock ticks; 0 while waiting for one
std::atomic<int64_t> g_busySince{0};
std::atomic<const char *> g_step{nullptr};

int64_t now()
{
  return Clock::now().time_since_epoch().count();
}

double seconds(int64_t ticks)
{
  return std::chrono::duration<double>(Clock::duration(ticks)).count();
}

} // namespace

void busy()
{
  g_busySince.store(now(), std::memory_order_relaxed);
}

void done()
{
  g_busySince.store(0, std::memory_order_relaxed);
}

Step::Step(const char *what) : m_outer(g_step.exchange(what, std::memory_order_relaxed))
{
  // nothing else
}

Step::~Step()
{
  g_step.store(m_outer, std::memory_order_relaxed);
}

void watch(const std::stop_token &stop)
{
  // the start of the event already reported, so each stall says so once
  int64_t reported = 0;
  const char *reportedStep = nullptr;
  while (!stop.stop_requested()) {
    std::this_thread::sleep_for(kCheckEvery);
    const auto since = g_busySince.load(std::memory_order_relaxed);
    if (reported != 0 && since != reported) {
      LOG_INFO(
          "the main loop runs again, after %.1f s on one event (%s)", seconds(now() - reported),
          reportedStep != nullptr ? reportedStep : "a step not named"
      );
      reported = 0;
    }
    if (since == 0 || since == reported || Clock::duration(now() - since) < kTooLong) {
      continue;
    }
    reportedStep = g_step.load(std::memory_order_relaxed);
    LOG_WARN(
        "the main loop has been on one event for %.1f s, in: %s; nothing else gets done meanwhile",
        seconds(now() - since), reportedStep != nullptr ? reportedStep : "a step not named"
    );
    reported = since;
  }
}

} // namespace deskflow::stall
