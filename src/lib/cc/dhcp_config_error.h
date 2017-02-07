// Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef DHCP_CONFIG_ERROR_H
#define DHCP_CONFIG_ERROR_H

#include <exceptions/exceptions.h>

namespace isc {

/// @brief Evaluation error exception raised when trying to parse.
///
/// This exception is expected to be thrown when parsing of the input
/// configuration has failed. This exception is used by parsers.
class ParseError : public isc::Exception {
 public:
 ParseError(const char* file, size_t line, const char* what) :
    isc::Exception(file, line, what) { };
};

namespace dhcp {

/// An exception that is thrown if an error occurs while configuring
/// DHCP server.
/// By convention when this exception is thrown there is a position
/// between parentheses so the code style should be like this:
///
/// try {
///     ...
/// } catch (const DhcpConfigError&) {
///     throw;
/// } catch (const std::exception& ex) {
///    isc_throw(DhcpConfigError, "message" << ex.what()
///              << " (" << getPosition(what) << ")");
/// }

/// @todo: move this header into simple_parser.h as soon as
/// there is no dependency through DhcpConfigParser
/// @todo: create an isc_throw like macro to add the
/// position more easily.
/// @todo: rename the exception for instance into ConfigError

class DhcpConfigError : public isc::Exception {
public:

    /// @brief constructor
    ///
    /// @param file name of the file, where exception occurred
    /// @param line line of the file, where exception occurred
    /// @param what text description of the issue that caused exception
    DhcpConfigError(const char* file, size_t line, const char* what)
        : isc::Exception(file, line, what) {}
};

}; // end of isc::dhcp namespace
}; // end of isc namespace

#endif // DHCP_CONFIG_ERROR_H
