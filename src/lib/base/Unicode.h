/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

//! Unicode utility functions
/*!
This class provides functions for converting between various Unicode
encodings and the current locale encoding.
*/
class Unicode
{
public:
  //! @name accessors
  //@{

  //! Test UTF-8 string for validity
  /*!
  Returns true iff the string contains a valid sequence of UTF-8
  encoded characters.
  */
  static bool isUTF8(const std::string &);

  //! Convert from UTF-8 to UCS-2 encoding
  /*!
  Convert from UTF-8 to UCS-2.  If errors is not nullptr then *errors
  is set to true iff any character could not be encoded in UCS-2.
  Decoding errors do not set *errors.
  */
  static std::string UTF8ToUCS2(const std::string &, bool *errors = nullptr);

  //! Convert from UTF-8 to UTF-16 encoding
  /*!
  Convert from UTF-8 to UTF-16.  If errors is not nullptr then *errors
  is set to true iff any character could not be encoded in UTF-16.
  Decoding errors do not set *errors.
  */
  static std::string UTF8ToUTF16(const std::string &, bool *errors = nullptr);

  //! Convert from UCS-2 to UTF-8
  /*!
  Convert from UCS-2 to UTF-8.  If errors is not nullptr then *errors is
  set to true iff any character could not be decoded.
  */
  static std::string UCS2ToUTF8(const std::string_view &, bool *errors = nullptr);

  //! Convert from UTF-16 to UTF-8
  /*!
  Convert from UTF-16 to UTF-8.  If errors is not nullptr then *errors is
  set to true iff any character could not be decoded.
  */
  static std::string UTF16ToUTF8(const std::string_view &, bool *errors = nullptr);

  //! Convert from UTF-8 to Latin-1
  /*!
  Characters outside Latin-1 and undecodable bytes become '?', as
  QString::toLatin1 does.  If errors is not nullptr then *errors is set to
  true iff any character was replaced.
  */
  static std::string UTF8ToLatin1(const std::string &, bool *errors = nullptr);

  //! Convert from Latin-1 to UTF-8
  static std::string Latin1ToUTF8(const std::string_view &);

  //! Length of the text without a UTF-8 sequence cut off at its end
  /*!
  For text read in chunks: the bytes past this length start a character
  whose remaining bytes haven't arrived yet.
  */
  static std::size_t completeUTF8Length(const std::string_view &);

  //@}

private:
  // internal conversion to UTF8
  static std::string doUCS2ToUTF8(const uint8_t *src, uint32_t n, bool *errors);
  static std::string doUTF16ToUTF8(const uint8_t *src, uint32_t n, bool *errors);

  // convert characters to/from UTF8
  static uint32_t fromUTF8(const uint8_t *&src, uint32_t &size);
  static void toUTF8(std::string &dst, uint32_t c, bool *errors);

private:
  static uint32_t s_invalid;
  static uint32_t s_replacement;
};
