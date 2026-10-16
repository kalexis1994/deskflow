/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "KeyModifierID.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

inline static const auto kModifierNameAlt = "alt";
inline static const auto kModifierNameAltGr = "altgr";
inline static const auto kModifierNameCtrl = "ctrl";
inline static const auto kModifierNameMeta = "meta";
inline static const auto kModifierNameNone = "none";
inline static const auto kModifierNameShift = "shift";
inline static const auto kModifierNameSuper = "super";

enum class KeyboardModifier : int8_t
{
  DefaultMod = -1,
  Shift,
  Ctrl,
  Alt,
  Meta,
  Super,
  AltGr,
  None
};

inline std::string keyboardModifierToOption(const KeyboardModifier modifier)
{
  using enum KeyboardModifier;
  switch (modifier) {
  case Alt:
    return kModifierNameAlt;
  case AltGr:
    return kModifierNameAltGr;
  case Ctrl:
    return kModifierNameCtrl;
  case Meta:
    return kModifierNameMeta;
  case None:
    return kModifierNameNone;
  case Shift:
    return kModifierNameShift;
  case Super:
    return kModifierNameSuper;
  default:
    return {};
  }
}

inline std::string valueToKeyboardModifierOption(int modifier)
{
  return keyboardModifierToOption(static_cast<KeyboardModifier>(modifier));
}

//! The modifier for a settings value; the names are matched ignoring case
inline KeyboardModifier keyboardModifierFromString(std::string_view modifier)
{
  const auto is = [modifier](std::string_view name) {
    return modifier.size() == name.size() &&
           std::equal(modifier.begin(), modifier.end(), name.begin(), [](char a, char b) {
             return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
  };
  using enum KeyboardModifier;
  if (is(kModifierNameShift))
    return Shift;
  if (is(kModifierNameCtrl))
    return Ctrl;
  if (is(kModifierNameAlt))
    return Alt;
  if (is(kModifierNameMeta))
    return Meta;
  if (is(kModifierNameSuper))
    return Super;
  if (is(kModifierNameAltGr))
    return AltGr;
  if (is(kModifierNameNone))
    return None;
  return DefaultMod;
}

inline int modifierValueFromString(std::string_view modifier)
{
  return static_cast<int>(keyboardModifierFromString(modifier));
}

inline int32_t modifierIDValueFromString(std::string_view modifier)
{
  using enum KeyboardModifier;
  switch (keyboardModifierFromString(modifier)) {
  case Shift:
    return static_cast<int32_t>(kKeyModifierIDShift);
  case Ctrl:
    return static_cast<int32_t>(kKeyModifierIDControl);
  case Alt:
    return static_cast<int32_t>(kKeyModifierIDAlt);
  case AltGr:
    return static_cast<int32_t>(kKeyModifierIDAltGr);
  case Meta:
    return static_cast<int32_t>(kKeyModifierIDMeta);
  case Super:
    return static_cast<int32_t>(kKeyModifierIDSuper);
  case None:
    return static_cast<int32_t>(kKeyModifierIDNull);
  default:
    return static_cast<int32_t>(-1);
  }
}
