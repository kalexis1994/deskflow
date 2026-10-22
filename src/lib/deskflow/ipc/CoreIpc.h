/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <string>

//! Sends a message to the GUIs connected to the core; queued until one connects (only
//! the latest of its command with latestOnly). Any thread.
void ipcSendToClient(const std::string &command, const std::string &args = {}, bool latestOnly = false);
void ipcSendConnectionState(deskflow::core::ConnectionState state);
