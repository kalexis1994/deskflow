# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME IniFileTests
  DEPENDS common
  SOURCE IniFileTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)

create_test(
  NAME CoreSettingsTests
  DEPENDS common
  SOURCE CoreSettingsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)

create_test(
  NAME LogLevelTests
  DEPENDS common
  SOURCE LogLevelTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/common"
)
