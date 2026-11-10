/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/Server.h"

void ServerTests::SwitchToComputerInfo_alloc_computer()
{
  auto actual = new Server::SwitchToComputerInfo("test");
  QCOMPARE(actual->m_computer, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndComputers()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_computers, "test");
  delete info;
}

using Names = std::vector<std::string>;

void ServerTests::hotkeyKeysGoOnlyWhereTheMouseIs()
{
  const Names clients = {"server", "laptop", "work"};
  QCOMPARE(Server::keyRecipients("*", "laptop", false, "", clients), Names{"laptop"});
  QCOMPARE(Server::keyRecipients(":laptop:work:", "work", false, "", clients), Names{"work"});
  // the mouse is on a computer the hotkey isn't for: nowhere, not to the others
  QCOMPARE(Server::keyRecipients(":laptop:work:", "server", false, "", clients), Names{});
  QCOMPARE(Server::keyRecipients(":laptop:", "work", false, "", clients), Names{});
  // the same while broadcasting the keyboard
  QCOMPARE(Server::keyRecipients(":laptop:work:", "work", true, "*", clients), Names{"work"});
}

void ServerTests::typedKeysGoWhereTheMouseIs()
{
  QCOMPARE(Server::keyRecipients("", "laptop", false, "", {"server", "laptop"}), Names{"laptop"});
  QCOMPARE(Server::keyRecipients(nullptr, "laptop", false, "", {"server", "laptop"}), Names{"laptop"});
}

void ServerTests::broadcastKeysGoToTheNamedComputers()
{
  const Names clients = {"laptop", "server", "work"};
  QCOMPARE(Server::keyRecipients("", "server", true, ":laptop:work:", clients), (Names{"laptop", "work"}));
  QCOMPARE(Server::keyRecipients("", "server", true, "", clients), clients);
}

QTEST_MAIN(ServerTests)
