// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Exceptions thrown by diffcore.

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace diffcore {

/// Base class of the exceptions diffcore throws.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A unified diff that does not parse. what() reads "line N: message";
/// line() is the 1-based line of the patch text the problem is on.
class ParseError : public Error {
public:
    ParseError(std::size_t line, const std::string& message)
        : Error("line " + std::to_string(line) + ": " + message), line_(line), message_(message) {}

    std::size_t line() const noexcept { return line_; }
    /// The message without the line number.
    const std::string& message() const noexcept { return message_; }

private:
    std::size_t line_;
    std::string message_;
};

/// The diff would need more than DiffOptions::max_cost edit steps, or the
/// inputs are too large to diff.
class LimitError : public Error {
public:
    using Error::Error;
};

}  // namespace diffcore
