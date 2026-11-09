/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureUtils.h"
#include "arch/OsDetection.h"

#include "arch/Utf8.h"
#include "base/FinalAction.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace deskflow {

namespace {

const EVP_MD *digestForType(Fingerprint::Type type)
{
  switch (type) {
  case Fingerprint::Type::Sha1:
    return EVP_sha1();
  case Fingerprint::Type::Sha256:
    return EVP_sha256();
  default:
    break;
  }
  throw std::runtime_error("Unknown fingerprint type " + std::to_string(static_cast<int>(type)));
}

} // namespace

std::string formatSSLFingerprint(const Fingerprint::Bytes &fingerprint, bool enableSeparators)
{
  auto hex = Fingerprint::toHex(fingerprint, enableSeparators ? ':' : '\0');
  std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return hex;
}

Fingerprint sslCertFingerprint(const X509 *cert, Fingerprint::Type type)
{
  if (!cert) {
    throw std::runtime_error("certificate is null");
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLength = 0;

  if (int result = X509_digest(cert, digestForType(type), digest, &digestLength); result <= 0) {
    throw std::runtime_error("failed to calculate fingerprint, digest result: " + std::to_string(result));
  }

  return {type, Fingerprint::Bytes(digest, digest + digestLength)};
}

void generatePemSelfSignedCert(const std::string &path, int keyLength)
{
  auto expirationDays = 365;

  auto *privateKey = EVP_RSA_gen(keyLength);
  if (!privateKey) {
    throw std::runtime_error("failed to generate a " + std::to_string(keyLength) + "bit key for certificate");
  }
  auto privateKeyFree = finally([privateKey]() { EVP_PKEY_free(privateKey); });

  auto *cert = X509_new();
  if (!cert) {
    throw std::runtime_error("could not allocate certificate");
  }
  auto certFree = finally([cert]() { X509_free(cert); });

  ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
  X509_gmtime_adj(X509_get_notBefore(cert), 0);
  X509_gmtime_adj(X509_get_notAfter(cert), expirationDays * 24 * 3600);
  X509_set_pubkey(cert, privateKey);

  auto *name = X509_NAME_new();
  if (!name) {
    throw std::runtime_error("could not allocate subject name");
  }
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("Deskflow"), -1, -1, 0);
  X509_set_subject_name(cert, name);
  X509_set_issuer_name(cert, name);
  X509_NAME_free(name);

  X509_sign(cert, privateKey, EVP_sha256());
  const auto fsPath = utf8Path(path);

#if defined(DESKFLOW_OS_WIN)
  auto fp = _wfopen(fsPath.native().c_str(), L"w");
#else
  auto fp = std::fopen(fsPath.native().c_str(), "w");
#endif

  if (!fp) {
    throw std::runtime_error("could not open certificate output path");
  }
  auto fileClose = finally([fp]() { std::fclose(fp); });

  PEM_write_PrivateKey(fp, privateKey, nullptr, nullptr, 0, nullptr, nullptr);
  PEM_write_X509(fp, cert);
}

bool ensurePemSelfSignedCert(const std::string &path, int keyLength)
{
  const auto fsPath = utf8Path(path);
  if (std::error_code ec; std::filesystem::exists(fsPath, ec))
    return false;
  if (fsPath.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(fsPath.parent_path(), ec);
    if (ec)
      throw std::runtime_error("could not create the certificate's folder: " + ec.message());
  }
  generatePemSelfSignedCert(path, keyLength);
  return true;
}

std::string formatSSLFingerprintColumns(const Fingerprint::Bytes &fingerprint)
{
  const size_t kBytesPerLine = 8;

  std::string result;
  for (size_t start = 0; start < fingerprint.size(); start += kBytesPerLine) {
    const auto end = (std::min)(start + kBytesPerLine, fingerprint.size()); // Windows.h defines min
    if (!result.empty())
      result += '\n';
    result += formatSSLFingerprint(Fingerprint::Bytes(fingerprint.begin() + start, fingerprint.begin() + end));
  }
  return result;
}

/*
    Draw an ASCII-Art representing the fingerprint so human brain can
    profit from its built-in pattern recognition ability.
    This technique is called "random art" and can be found in some
    scientific publications like this original paper:
    "Hash Visualization: a New Technique to improve Real-World Security",
    Perrig A. and Song D., 1999, International Workshop on Cryptographic
    Techniques and E-Commerce (CrypTEC '99)
    sparrow.ece.cmu.edu/~adrian/projects/validation/validation.pdf
    The subject came up in a talk by Dan Kaminsky, too.
    If you see the picture is different, the key is different.
    If the picture looks the same, you still know nothing.
    The algorithm used here is a worm crawling over a discrete plane,
    leaving a trace (augmenting the field) everywhere it goes.
    Movement is taken from rawDigest 2bit-wise.  Bumping into walls
    makes the respective movement vector be ignored for this turn.
    Graphs are not unambiguous, because circles in graphs can be
walked in either direction.
 */

/*
    Field sizes for the random art.  Have to be odd, so the starting point
    can be in the exact middle of the picture, and `baseSize` should be >=8 .
    Else pictures would be too dense, and drawing the frame would
    fail, too, because the key type would not fit in anymore.
*/

std::string generateFingerprintArt(const Fingerprint::Bytes &rawDigest)
{
  const auto baseSize = 8;
  const auto rows = (baseSize + 1);
  const auto columns = (baseSize * 2 + 1);
  const std::string_view characterPool = " .o+=*BOX@%&#/^SE";
  const std::size_t len = characterPool.length() - 1;

  std::uint8_t field[columns][rows];
  memset(field, 0, columns * rows * sizeof(char));
  int x = columns / 2;
  int y = rows / 2;

  /* process raw key */
  for (int byte : rawDigest) {
    /* each byte conveys four 2-bit move commands */
    for (uint32_t b = 0; b < 4; b++) {
      /* evaluate 2 bit, rest is shifted later */
      x += (byte & 0x1) ? 1 : -1;
      y += (byte & 0x2) ? 1 : -1;

      /* assure we are still in bounds */
      x = std::clamp(x, 0, columns - 1);
      y = std::clamp(y, 0, rows - 1);

      /* augment the field */
      if (field[x][y] < len - 2)
        field[x][y]++;
      byte = byte >> 2;
    }
  }

  /* mark starting point and end point*/
  field[columns / 2][rows / 2] = len - 1;
  field[x][y] = len;

  std::string result;
  result.reserve((columns + 6) * (rows + 2) * 3);
  result.append("╔═════════════════╗\n");

  /* output content */
  for (y = 0; y < rows; y++) {
    result.append("║");
    for (x = 0; x < columns; x++)
      result += characterPool.at(std::min<std::size_t>(field[x][y], len));
    result.append("║\n");
  }

  result.append("╚═════════════════╝");
  return result;
}

} // namespace deskflow
