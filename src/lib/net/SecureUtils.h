/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Fingerprint.h"

#include <openssl/x509.h>

#include <string>

namespace deskflow {

/**
 * @brief formatSSLFingerprint Format an ssl Fingerprint
 * @param fingerprint the raw digest
 * @param enableSeparators insert : seperator every byte when true
 * @return uppercase hex, e.g. "28:FD:0A:..."
 */
std::string formatSSLFingerprint(const Fingerprint::Bytes &fingerprint, bool enableSeparators = true);

//! The hex fingerprint broken into lines of 8 bytes
std::string formatSSLFingerprintColumns(const Fingerprint::Bytes &fingerprint);

Fingerprint sslCertFingerprint(const X509 *cert, Fingerprint::Type type);

//! Writes a new key and self-signed certificate to a PEM file (UTF-8 path)
void generatePemSelfSignedCert(const std::string &path, int keyLength = 2048);

//! The "random art" picture of a digest (UTF-8, box-drawing frame)
std::string generateFingerprintArt(const Fingerprint::Bytes &rawDigest);
} // namespace deskflow
