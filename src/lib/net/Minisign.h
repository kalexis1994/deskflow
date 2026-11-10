/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

//! Minisign signatures, as `tauri signer` makes them for updates (docs/dev/updates.md)
/*!
A public key is two lines: a comment, then base64 of "Ed", an 8-byte key id and the
32-byte Ed25519 key. A signature is four lines: a comment; base64 of "ED", the key id
and the Ed25519 signature of the BLAKE2b-512 of the file ("Ed": of the file itself);
"trusted comment: " and its text; and base64 of the signature of the first signature
followed by that text. Tauri stores both as base64 of the whole text, which is
accepted too.
*/
namespace deskflow::minisign {

//! The trusted comment of `signature` if it signs `data` with `publicKey`; nothing if
//! anything about it is wrong
std::optional<std::string> verify(std::string_view publicKey, std::string_view signature, std::string_view data);

//! Standard base64, padded or not; nothing if it isn't base64
std::optional<std::string> decodeBase64(std::string_view text);

} // namespace deskflow::minisign
