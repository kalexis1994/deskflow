/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsUpdateTests.h"

#include "net/Minisign.h"
#include "platform/MSWindowsUpdate.h"

#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <fstream>

namespace fs = std::filesystem;
using namespace deskflow::platform::update;

namespace {

// `tauri signer` with a key made only for these tests, over kInstaller saved as
// Deskflow-Tauri_0.2.0_x64-setup.exe
constexpr auto kTestKey = "dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDIyQTg4MjIwMDZFMTEwRUEKUldUcUVPRUdJSUtv"
                          "SXU2SzhsbWRjcHdKbHc2WTky"
                          "YnNFdnkrWlRzYnJ3aG1MT1FGVFZZM1N6OFAK";
constexpr auto kInstaller = "not really an installer\n";
constexpr auto kSignature = "dW50cnVzdGVkIGNvbW1lbnQ6IHNpZ25hdHVyZSBmcm9tIHRhdXJpIHNlY3JldCBrZXkKUlVUcUVPRUdJSUtvSWdGaH"
                            "RTVWRkZnhjak9uMmVvbEs4aTBJ"
                            "dnZ3Y0JvQUtzb3lUd1RHaDVNZTlac01zYzNOYnVtcVc0OGlyc3ppRlN3U3dmK2t5YkV6R1laa2E3MW1SZmcwPQp0cn"
                            "VzdGVkIGNvbW1lbnQ6IHRpbWVz"
                            "dGFtcDoxNzk0MjM5NTA0CWZpbGU6RGVza2Zsb3ctVGF1cmlfMC4yLjBfeDY0LXNldHVwLmV4ZQpwTE10blZmS01veG"
                            "NXMFp4OWNSd1Q5WnNqamdBZDFp"
                            "TDNFSWtQTmpUNXc0UllpQllHaURrVkRtQlB5WTdxcGpCcTQ3MUU2UWFqc05Dd3NiSmVBVVVEQT09Cg==";

void write(const fs::path &path, const std::string &data)
{
  std::ofstream(path, std::ios::binary) << data;
}

std::string read(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

//! The download, as the app leaves it for the service
fs::path download(const fs::path &dir, const std::string &installer = kInstaller, bool signature = true)
{
  const auto path = dir / "Deskflow-Tauri_0.2.0_x64-setup.exe";
  write(path, installer);
  const auto signaturePath = fs::path(path) += ".sig";
  if (signature)
    write(signaturePath, *deskflow::minisign::decodeBase64(kSignature));
  else
    fs::remove(signaturePath); // the tests share the folder
  return path;
}

std::string errorOf(const std::variant<fs::path, std::string> &result)
{
  const auto *error = std::get_if<std::string>(&result);
  return error ? *error : std::string();
}

} // namespace

void MSWindowsUpdateTests::init()
{
  QVERIFY(m_dir.isValid());
}

void MSWindowsUpdateTests::parsesVersions()
{
  QVERIFY(parseVersion("0.1.12") == (Version{0, 1, 12}));
  QVERIFY(parseVersion("10.0.0") == (Version{10, 0, 0}));
  QVERIFY(!parseVersion("0.1").has_value());
  QVERIFY(!parseVersion("0.1.12.4").has_value());
  QVERIFY(!parseVersion("0.1.x").has_value());
  QVERIFY(!parseVersion("").has_value());
  QVERIFY(!parseVersion("0..1").has_value());
  QVERIFY((Version{0, 1, 10}) > (Version{0, 1, 9}));
}

void MSWindowsUpdateTests::readsTheSignedVersion()
{
  QVERIFY(signedVersion("timestamp:1794239201\tfile:Deskflow-Tauri_0.1.12_x64-setup.exe") == (Version{0, 1, 12}));
  QVERIFY(!signedVersion("timestamp:1\tfile:data.txt").has_value());
  QVERIFY(!signedVersion("timestamp:1\tfile:Deskflow-Tauri_0.1.12_x64-setup.msi").has_value());
  QVERIFY(!signedVersion("timestamp:1").has_value());
}

void MSWindowsUpdateTests::copiesANewerSignedInstaller()
{
  const fs::path dir = m_dir.path().toStdWString();
  const auto result = prepare(download(dir), Version{0, 1, 12}, dir / "updates", kTestKey);
  QVERIFY2(std::holds_alternative<fs::path>(result), errorOf(result).c_str());
  const auto &copy = std::get<fs::path>(result);
  QCOMPARE(copy.filename().string(), std::string("Deskflow-Tauri_0.2.0_x64-setup.exe"));
  QCOMPARE(copy.parent_path().parent_path(), dir / "updates");
  QCOMPARE(read(copy), std::string(kInstaller));

  // only SYSTEM and administrators, and nothing inherited from a folder others can write
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  QCOMPARE(
      GetNamedSecurityInfoW(
          copy.parent_path().c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr,
          &descriptor
      ),
      static_cast<DWORD>(ERROR_SUCCESS)
  );
  LPWSTR text = nullptr;
  QVERIFY(ConvertSecurityDescriptorToStringSecurityDescriptorW(
      descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &text, nullptr
  ));
  const std::wstring dacl = text;
  LocalFree(text);
  LocalFree(descriptor);
  QVERIFY2(dacl.starts_with(L"D:P"), QString::fromStdWString(dacl).toUtf8());
  QCOMPARE(QString::fromStdWString(dacl).count(QStringLiteral("(A;")), 2);
  QVERIFY(dacl.find(L";;;SY)") != std::wstring::npos);
  QVERIFY(dacl.find(L";;;BA)") != std::wstring::npos);
}

void MSWindowsUpdateTests::refusesWhatIsntNewer()
{
  const fs::path dir = m_dir.path().toStdWString();
  QCOMPARE(
      errorOf(prepare(download(dir), Version{0, 2, 0}, dir / "updates", kTestKey)),
      std::string("version 0.2.0 is not newer than 0.2.0")
  );
  QVERIFY(!errorOf(prepare(download(dir), Version{1, 0, 0}, dir / "updates", kTestKey)).empty());
}

void MSWindowsUpdateTests::refusesAnEditedInstaller()
{
  const fs::path dir = m_dir.path().toStdWString();
  QCOMPARE(
      errorOf(prepare(download(dir, "something else\n"), Version{0, 1, 0}, dir / "updates", kTestKey)),
      std::string("the installer's signature is not valid")
  );
}

void MSWindowsUpdateTests::refusesAnotherKey()
{
  // the real key: the test installer isn't signed with it
  const fs::path dir = m_dir.path().toStdWString();
  QCOMPARE(
      errorOf(prepare(download(dir), Version{0, 1, 0}, dir / "updates")),
      std::string("the installer's signature is not valid")
  );
}

void MSWindowsUpdateTests::refusesWithoutSignature()
{
  const fs::path dir = m_dir.path().toStdWString();
  QCOMPARE(
      errorOf(prepare(download(dir, kInstaller, false), Version{0, 1, 0}, dir / "updates", kTestKey)),
      std::string("could not read the installer and its signature")
  );
}

void MSWindowsUpdateTests::refusesWithoutAnInstalledVersion()
{
  const fs::path dir = m_dir.path().toStdWString();
  QVERIFY(!errorOf(prepare(download(dir), std::nullopt, dir / "updates", kTestKey)).empty());
}

void MSWindowsUpdateTests::startsFromAnEmptyFolderEachTime()
{
  const fs::path dir = m_dir.path().toStdWString();
  const auto first = std::get<fs::path>(prepare(download(dir), Version{0, 1, 0}, dir / "updates", kTestKey));
  const auto second = std::get<fs::path>(prepare(download(dir), Version{0, 1, 0}, dir / "updates", kTestKey));
  QVERIFY(first.parent_path() != second.parent_path());
  QVERIFY(!fs::exists(first.parent_path()));
  QVERIFY(fs::exists(second));
}

QTEST_MAIN(MSWindowsUpdateTests)
