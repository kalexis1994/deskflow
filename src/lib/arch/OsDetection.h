/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

// Platform detection for the core, with the same meaning as Qt's Q_OS_* macros
// (which the core used before it stopped depending on Qt):
//   DESKFLOW_OS_WIN    Windows
//   DESKFLOW_OS_MACOS  macOS (also DESKFLOW_OS_UNIX)
//   DESKFLOW_OS_LINUX  Linux (also DESKFLOW_OS_UNIX)
//   DESKFLOW_OS_UNIX   any Unix-like system, including the BSDs

#if defined(_WIN32)
#define DESKFLOW_OS_WIN 1
#elif defined(__APPLE__) && defined(__MACH__)
#define DESKFLOW_OS_MACOS 1
#define DESKFLOW_OS_UNIX 1
#elif defined(__linux__)
#define DESKFLOW_OS_LINUX 1
#define DESKFLOW_OS_UNIX 1
#elif defined(__unix__)
#define DESKFLOW_OS_UNIX 1
#else
#error "unsupported platform"
#endif
