/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <string>

void ipcSendToClient(const std::string &command, const std::string &args = {});
void ipcSendConnectionState(deskflow::core::ConnectionState state);
