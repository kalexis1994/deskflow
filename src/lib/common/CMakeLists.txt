# SPDX-FileCopyrightText: (C) 2024 - 2025 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

set(CORE_BINARY "${CMAKE_PROJECT_NAME}-core")
if(WIN32)
  string(APPEND CORE_BINARY ".exe")
endif()

configure_file(Constants.h.in Constants.h @ONLY)
configure_file(VersionInfo.h.in VersionInfo.h @ONLY)

# Headers the low-level libraries (arch, base, io, mt) need, without Qt
add_library(common-base INTERFACE
  ExitCodes.h
  LogLevel.h
  ${CMAKE_CURRENT_BINARY_DIR}/Constants.h
)

target_include_directories(common-base
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/lib>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src/lib>
)

add_library(common STATIC
  Coordinate.h
  Enums.h
  ExitCodes.h
  KeyModifierID.h
  KeyboardModifier.h
  LogLevel.h
  NetworkProtocol.h
  PlatformInfo.h
  CoreSettings.cpp
  CoreSettings.h
  IniFile.cpp
  IniFile.h
  UrlConstants.h
  ${CMAKE_CURRENT_BINARY_DIR}/Constants.h
  ${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.h
)

# Qt6::Core is only left for the moc'd enums in Enums.h and Coordinate.h (IPC, phase 6)
target_link_libraries(common PUBLIC common-base base Qt6::Core)
if(WIN32)
  # GetUserProfileDirectoryW, for the settings file's home directory
  target_link_libraries(common PRIVATE userenv)
endif()

target_include_directories(common
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/lib>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src/lib>
)
