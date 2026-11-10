/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MinisignTests.h"

#include "net/Minisign.h"

using deskflow::minisign::decodeBase64;
using deskflow::minisign::verify;

namespace {

// `tauri signer generate` and `tauri signer sign` with a key made only for these tests
constexpr auto kPublicKey = "dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDIyQTg4MjIwMDZFMTEwRUEKUldUcUVPRUdJSU"
                            "tvSXU2SzhsbWRjcHdKbHc2WTky"
                            "YnNFdnkrWlRzYnJ3aG1MT1FGVFZZM1N6OFAK";
constexpr auto kData = "deskflow minisign test\n";
constexpr auto kSignature = "dW50cnVzdGVkIGNvbW1lbnQ6IHNpZ25hdHVyZSBmcm9tIHRhdXJpIHNlY3JldCBrZXkKUlVUcUVPRUdJSUtvSW03Tk"
                            "dlYTA1MUs0OTBvUjlrbVlNUU9V"
                            "WXIzeUM3OGl5QzMvZUcrNEU1R3pIdVFSbGxZQnVjTW5XRVIyb0c5UzkwcFZKaEFsclJyd25kSFovNkh0Znd3PQp0cn"
                            "VzdGVkIGNvbW1lbnQ6IHRpbWVz"
                            "dGFtcDoxNzk0MjM5NDQwCWZpbGU6ZGF0YS50eHQKUmZkUDN6a0FTcmVlZ0tEUzhKZlkwRnZvbDMrT3FvSC9xL3o3TG"
                            "U5TFNQVERSSUJVKzlEL1Myc3U0"
                            "VXdLb0tmSmRVbnFqc0plcXdEQmxPZGhNaWtXQkE9PQo=";
// the real updates key: right format, wrong key for these signatures
constexpr auto kOtherKey = "dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDNDRThDMkQzNTE1NUIzNjMKUldSanMxVlIwOEx"
                           "vUE05bGVtOGo3emQ4bTJVb3Rh"
                           "UlU3UmZRWWZ2VEpranZWS2U5TmJMNW9kRWIK";

std::string signatureText()
{
  return *decodeBase64(kSignature);
}

} // namespace

void MinisignTests::acceptsWhatTauriSigned()
{
  const auto trusted = verify(kPublicKey, kSignature, kData);
  QVERIFY(trusted.has_value());
  QCOMPARE(*trusted, std::string("timestamp:1794239440\tfile:data.txt"));
}

void MinisignTests::acceptsTheTextItself()
{
  QVERIFY(verify(*decodeBase64(kPublicKey), signatureText(), kData).has_value());
}

void MinisignTests::rejectsOtherData()
{
  QVERIFY(!verify(kPublicKey, kSignature, "deskflow minisign test!\n").has_value());
  QVERIFY(!verify(kPublicKey, kSignature, "").has_value());
}

void MinisignTests::rejectsAnEditedTrustedComment()
{
  auto text = signatureText();
  const auto at = text.find("file:data.txt");
  QVERIFY(at != std::string::npos);
  text.replace(at, 13, "file:evil.txt");
  QVERIFY(!verify(kPublicKey, text, kData).has_value());
}

void MinisignTests::rejectsAnotherKey()
{
  QVERIFY(!verify(kOtherKey, kSignature, kData).has_value());
}

void MinisignTests::rejectsGarbage()
{
  QVERIFY(!verify("", "", kData).has_value());
  QVERIFY(!verify(kPublicKey, "not a signature", kData).has_value());
  QVERIFY(!verify("not a key", kSignature, kData).has_value());

  // three lines instead of four
  auto text = signatureText();
  text.resize(text.rfind("trusted comment"));
  QVERIFY(!verify(kPublicKey, text, kData).has_value());
}

void MinisignTests::decodesBase64()
{
  QCOMPARE(*decodeBase64("aGVsbG8="), std::string("hello"));
  QCOMPARE(*decodeBase64("aGVsbG8"), std::string("hello"));
  QCOMPARE(*decodeBase64(""), std::string());
  QVERIFY(!decodeBase64("aGV$bG8=").has_value());
  QVERIFY(!decodeBase64("aGVsbG8=aGVs").has_value());
}

QTEST_MAIN(MinisignTests)
