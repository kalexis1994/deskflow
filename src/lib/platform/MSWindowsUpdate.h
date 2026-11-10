/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <compare>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

//! Installing an update from the service, without asking Windows for permission
/*!
The app asks for it once the person has said yes (docs/dev/updates.md). Any program
can reach the service's pipe, so the service trusts nothing in the request: it copies
the installer into a new folder only SYSTEM and administrators can write, checks the
copy's signature against the key built in here, and only takes a version newer than
the installed one.
*/
namespace deskflow::platform::update {

//! The key `tauri.conf.json` has as `plugins.updater.pubkey`; a test checks they match
inline constexpr auto kPublicKey = "dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDNDRThDMkQzNTE1NUIzNjMKUldSanM"
                                   "xVlIwOExvUE05bGVtOGo3emQ4bTJVb3Rh"
                                   "UlU3UmZRWWZ2VEpranZWS2U5TmJMNW9kRWIK";

struct Version
{
  int major = 0;
  int minor = 0;
  int patch = 0;
  auto operator<=>(const Version &) const = default;
};

//! "0.1.12"; nothing for anything else
std::optional<Version> parseVersion(std::string_view text);

//! The version of the installer a signature's trusted comment names
//! ("timestamp:…\tfile:Deskflow-Tauri_0.1.12_x64-setup.exe")
std::optional<Version> signedVersion(std::string_view trustedComment);

//! What Windows lists as installed; nothing if it isn't
std::optional<Version> installedVersion();

//! Copies `installer` and `installer.sig` into a new folder under `root` and checks them
/*!
\return the copy to run, or why not
*/
std::variant<std::filesystem::path, std::string> prepare(
    const std::filesystem::path &installer, std::optional<Version> installed, const std::filesystem::path &root,
    std::string_view publicKey = kPublicKey
);

//! `root` for `prepare`: under ProgramData, next to the service's log
std::filesystem::path defaultRoot();

} // namespace deskflow::platform::update
