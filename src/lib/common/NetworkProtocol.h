/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

inline static const auto kSynergyProtocolOption = "synergy";
inline static const auto kBarrierProtocolOption = "barrier";

enum class NetworkProtocol
{
  Unknown = -1,
  Synergy,
  Barrier
};

inline std::string networkProtocolToOption(const NetworkProtocol proto)
{
  switch (proto) {
  case NetworkProtocol::Synergy:
    return kSynergyProtocolOption;
  case NetworkProtocol::Barrier:
    return kBarrierProtocolOption;
  default:
    return {};
  }
}

inline std::string networkProtocolToName(const NetworkProtocol proto)
{
  switch (proto) {
  case NetworkProtocol::Synergy:
    return "Synergy";
  case NetworkProtocol::Barrier:
    return "Barrier";
  default:
    return {};
  }
}

inline NetworkProtocol networkProtocolFromString(std::string_view proto)
{
  const auto equalsIgnoreCase = [proto](std::string_view option) {
    return proto.size() == option.size() && std::equal(proto.begin(), proto.end(), option.begin(), [](char a, char b) {
             return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
  };
  using enum NetworkProtocol;
  if (equalsIgnoreCase(kBarrierProtocolOption))
    return Barrier;
  if (equalsIgnoreCase(kSynergyProtocolOption))
    return Synergy;
  return Unknown;
}
