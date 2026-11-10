/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsUpdate.h"

#include "net/Minisign.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>

#include <charconv>
#include <fstream>
#include <iterator>
#include <random>

namespace fs = std::filesystem;

namespace deskflow::platform::update {

namespace {

constexpr std::uintmax_t kMaxInstallerSize = 512ull * 1024 * 1024;
constexpr std::string_view kFilePrefix = "file:Deskflow-Tauri_";
constexpr std::string_view kFileSuffix = "_x64-setup.exe";
constexpr auto kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Deskflow Tauri";
//! SYSTEM and administrators only, nothing inherited: no one else can put a file in it
constexpr auto kFolderSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

std::optional<std::string> readFile(const fs::path &path)
{
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size > kMaxInstallerSize)
    return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  std::string data(static_cast<std::size_t>(size), '\0');
  if (!in.read(data.data(), static_cast<std::streamsize>(data.size())))
    return std::nullopt;
  return data;
}

bool writeFile(const fs::path &path, const std::string &data)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  return out.write(data.data(), static_cast<std::streamsize>(data.size())) && out.flush();
}

std::wstring randomName()
{
  std::random_device random;
  std::wstring name = L"update-";
  for (int i = 0; i < 4; ++i) {
    wchar_t part[9];
    swprintf(part, std::size(part), L"%08x", random());
    name += part;
  }
  return name;
}

//! A folder made here, now, with `kFolderSddl`: one that already exists could be
//! anyone's, who could swap the file after it is checked
std::optional<fs::path> newPrivateFolder(const fs::path &root)
{
  std::error_code ec;
  fs::create_directories(root, ec);

  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kFolderSddl, SDDL_REVISION_1, &descriptor, nullptr))
    return std::nullopt;
  SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
  const auto folder = root / randomName();
  const bool made = CreateDirectoryW(folder.c_str(), &attributes) != FALSE;
  LocalFree(descriptor);
  if (!made)
    return std::nullopt;
  return folder;
}

//! Folders of earlier updates, which nothing runs from any more
void removeEarlier(const fs::path &root)
{
  std::error_code ec;
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_directory() && it->path().filename().native().starts_with(L"update-")) {
      std::error_code ignored;
      fs::remove_all(it->path(), ignored);
    }
  }
}

std::string toString(const Version &version)
{
  return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

} // namespace

std::optional<Version> parseVersion(std::string_view text)
{
  Version version;
  int *parts[] = {&version.major, &version.minor, &version.patch};
  for (std::size_t i = 0; i < std::size(parts); ++i) {
    const auto end = i + 1 < std::size(parts) ? text.find('.') : text.size();
    if (end == std::string_view::npos || end == 0)
      return std::nullopt;
    const auto field = text.substr(0, end);
    const auto [rest, error] = std::from_chars(field.data(), field.data() + field.size(), *parts[i]);
    if (error != std::errc() || rest != field.data() + field.size() || *parts[i] < 0)
      return std::nullopt;
    text.remove_prefix(i + 1 < std::size(parts) ? end + 1 : end);
  }
  return version;
}

std::optional<Version> signedVersion(std::string_view trustedComment)
{
  const auto start = trustedComment.find(kFilePrefix);
  if (start == std::string_view::npos)
    return std::nullopt;
  auto name = trustedComment.substr(start + kFilePrefix.size());
  if (const auto tab = name.find('\t'); tab != std::string_view::npos)
    name = name.substr(0, tab);
  if (!name.ends_with(kFileSuffix))
    return std::nullopt;
  name.remove_suffix(kFileSuffix.size());
  return parseVersion(name);
}

std::optional<Version> installedVersion()
{
  wchar_t value[64] = {};
  DWORD size = sizeof(value);
  if (RegGetValueW(
          HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayVersion", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value,
          &size
      ) != ERROR_SUCCESS)
    return std::nullopt;
  std::string text;
  for (const wchar_t *c = value; *c != L'\0'; ++c) {
    if (*c > 0x7F)
      return std::nullopt;
    text.push_back(static_cast<char>(*c));
  }
  return parseVersion(text);
}

fs::path defaultRoot()
{
  wchar_t programData[MAX_PATH] = {};
  const auto length = GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH);
  const fs::path base = length > 0 && length < MAX_PATH ? fs::path(programData) : fs::path(L"C:\\ProgramData");
  return base / L"Deskflow" / L"updates";
}

std::variant<fs::path, std::string>
prepare(const fs::path &installer, std::optional<Version> installed, const fs::path &root, std::string_view publicKey)
{
  if (!installed)
    return std::string("no installed version to compare with");

  // read once: what is checked below is exactly what gets written and run
  const auto data = readFile(installer);
  auto signaturePath = installer;
  signaturePath += L".sig";
  const auto signature = readFile(signaturePath);
  if (!data || !signature)
    return std::string("could not read the installer and its signature");

  const auto trusted = minisign::verify(publicKey, *signature, *data);
  if (!trusted)
    return std::string("the installer's signature is not valid");
  const auto version = signedVersion(*trusted);
  if (!version)
    return std::string("the signature is not of a Deskflow installer");
  if (*version <= *installed)
    return "version " + toString(*version) + " is not newer than " + toString(*installed);

  removeEarlier(root);
  const auto folder = newPrivateFolder(root);
  if (!folder)
    return std::string("could not make a private folder for the installer");
  const auto copy = *folder / ("Deskflow-Tauri_" + toString(*version) + "_x64-setup.exe");
  if (!writeFile(copy, *data))
    return std::string("could not copy the installer");
  return copy;
}

} // namespace deskflow::platform::update
