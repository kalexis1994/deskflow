/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/XWindowsClipboardTextConverter.h"

#include "base/Unicode.h"

//
// XWindowsClipboardTextConverter
//

XWindowsClipboardTextConverter::XWindowsClipboardTextConverter(Display *display, const char *name)
    : m_atom(XInternAtom(display, name, False))
{
  // do nothing
}

IClipboard::Format XWindowsClipboardTextConverter::getFormat() const
{
  return IClipboard::Format::Text;
}

Atom XWindowsClipboardTextConverter::getAtom() const
{
  return m_atom;
}

int XWindowsClipboardTextConverter::getDataSize() const
{
  return 8;
}

std::string XWindowsClipboardTextConverter::fromIClipboard(const std::string &data) const
{
  return Unicode::UTF8ToLatin1(data);
}

std::string XWindowsClipboardTextConverter::toIClipboard(const std::string &data) const
{
  return Unicode::Latin1ToUTF8(data);
}
