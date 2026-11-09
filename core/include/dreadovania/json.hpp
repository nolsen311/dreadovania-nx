// SPDX-License-Identifier: GPL-3.0-or-later
// A small JSON reader and string encoder, enough for the server's API and the
// app's own settings file. No exceptions (libnx builds use -fno-exceptions),
// limited depth and size, and the same code on the Switch and in the tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dreadovania::json {

inline constexpr std::size_t kMaxDepth = 32;
inline constexpr std::size_t kMaxInput = 4 * 1024 * 1024;

class Value {
  public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // nullptr unless this is an object with that key (the last one, if repeated).
    const Value* get(std::string_view key) const;
    // The string value of a member, or nothing if it's missing or not a string.
    std::optional<std::string> string_at(std::string_view key) const;
    std::optional<bool> bool_at(std::string_view key) const;
    std::optional<std::int64_t> int_at(std::string_view key) const;

    const std::string& as_string() const { return string_; }
    bool as_bool() const { return bool_; }
    double as_number() const { return number_; }
    const std::vector<Value>& items() const { return items_; }
    const std::vector<std::pair<std::string, Value>>& members() const { return members_; }

  private:
    friend class Parser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Value> items_;
    std::vector<std::pair<std::string, Value>> members_;
};

struct ParseResult {
    std::optional<Value> value;
    std::string error;  // empty on success
};

ParseResult parse(std::string_view text);

// A JSON string literal, quotes included, for building request bodies.
std::string quote(std::string_view text);

}  // namespace dreadovania::json
