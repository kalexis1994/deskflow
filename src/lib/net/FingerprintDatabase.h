/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Fingerprint.h"

#include <iosfwd>
#include <string>
#include <vector>

//! The trusted fingerprints file: one `Fingerprint::toDbLine()` per line
class FingerprintDatabase
{
public:
  //! Adds the fingerprints in a file (UTF-8 path); a missing file adds nothing
  void read(const std::string &path);
  bool write(const std::string &path);

  void readStream(std::istream &in);
  bool writeStream(std::ostream &out);

  void clear();
  void addTrusted(const Fingerprint &fingerprint);
  bool isTrusted(const Fingerprint &fingerprint) const;

  const std::vector<Fingerprint> &fingerprints() const
  {
    return m_fingerprints;
  }

private:
  std::vector<Fingerprint> m_fingerprints;
};
