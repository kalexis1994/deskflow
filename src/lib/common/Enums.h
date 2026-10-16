/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2010 - 2018, 2024 - 2025 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 - 2007 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deskflow {

//! Names of an enum's values, in value order; the IPC protocol sends these names
template <typename Enum> struct EnumNames;

//! The name of an enum value ("Connected"), empty if out of range
template <typename Enum> constexpr std::string_view enumToString(Enum value)
{
  const auto index = static_cast<std::size_t>(value);
  return index < EnumNames<Enum>::names.size() ? EnumNames<Enum>::names[index] : std::string_view();
}

//! The enum value with this exact name, as QMetaEnum::keyToValue did
template <typename Enum> constexpr std::optional<Enum> enumFromString(std::string_view name)
{
  for (std::size_t i = 0; i < EnumNames<Enum>::names.size(); ++i) {
    if (EnumNames<Enum>::names[i] == name)
      return static_cast<Enum>(i);
  }
  return std::nullopt;
}

} // namespace deskflow

namespace deskflow::client {
enum class ErrorType : uint8_t
{
  NoError,
  AlreadyConnected,
  HostnameError,
  GenericError
};
} // namespace deskflow::client

namespace deskflow::core {

enum class ProcessState
{
  Starting,
  Started,
  Stopping,
  Stopped,
  RetryPending
};

enum class ConnectionState
{
  Disconnected,
  Connecting,
  Connected,
  Listening
};

enum class ConnectionRefusal
{
  IncompatibleVersion,
  AlreadyConnected,
  UnknownClient,
  ProtocolError
};

} // namespace deskflow::core

template <> struct deskflow::EnumNames<deskflow::client::ErrorType>
{
  static constexpr std::array<std::string_view, 4> names{
      "NoError", "AlreadyConnected", "HostnameError", "GenericError"
  };
};

template <> struct deskflow::EnumNames<deskflow::core::ProcessState>
{
  static constexpr std::array<std::string_view, 5> names{"Starting", "Started", "Stopping", "Stopped", "RetryPending"};
};

template <> struct deskflow::EnumNames<deskflow::core::ConnectionState>
{
  static constexpr std::array<std::string_view, 4> names{"Disconnected", "Connecting", "Connected", "Listening"};
};

template <> struct deskflow::EnumNames<deskflow::core::ConnectionRefusal>
{
  static constexpr std::array<std::string_view, 4> names{
      "IncompatibleVersion", "AlreadyConnected", "UnknownClient", "ProtocolError"
  };
};
