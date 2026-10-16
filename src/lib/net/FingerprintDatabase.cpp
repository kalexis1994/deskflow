/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FingerprintDatabase.h"

#include "arch/Utf8.h"

#include <algorithm>
#include <fstream>

void FingerprintDatabase::read(const std::string &path)
{
  std::ifstream file(deskflow::utf8Path(path), std::ios::binary);
  if (!file)
    return;
  readStream(file);
}

void FingerprintDatabase::readStream(std::istream &in)
{
  std::string line;
  while (std::getline(in, line)) {
    if (line.ends_with('\r'))
      line.pop_back();
    if (line.empty())
      continue;
    auto fingerprint = Fingerprint::fromDbLine(line);
    if (!fingerprint.isValid()) {
      continue;
    }
    m_fingerprints.push_back(std::move(fingerprint));
  }
}

bool FingerprintDatabase::write(const std::string &path)
{
  std::ofstream file(deskflow::utf8Path(path), std::ios::binary | std::ios::trunc);
  if (!file)
    return false;
  return writeStream(file);
}

bool FingerprintDatabase::writeStream(std::ostream &out)
{
  if (!out)
    return false;

  for (const auto &fingerprint : m_fingerprints) {
    out << fingerprint.toDbLine() << "\n";
  }
  out.flush();
  return static_cast<bool>(out);
}

void FingerprintDatabase::clear()
{
  m_fingerprints.clear();
}

void FingerprintDatabase::addTrusted(const Fingerprint &fingerprint)
{
  if (isTrusted(fingerprint)) {
    return;
  }
  m_fingerprints.push_back(fingerprint);
}

bool FingerprintDatabase::isTrusted(const Fingerprint &fingerprint) const
{
  return std::find(m_fingerprints.begin(), m_fingerprints.end(), fingerprint) != m_fingerprints.end();
}
