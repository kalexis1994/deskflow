/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#import <CoreFoundation/CoreFoundation.h>

#if defined(__cplusplus)
extern "C"
{
#endif
  void runCocoaApp();
  void stopCocoaLoop();
  //! Creates NSApp without running it, as QApplication did for the core
  void initCocoaApp();

#if defined(__cplusplus)
}
#endif
