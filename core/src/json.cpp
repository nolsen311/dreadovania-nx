// SPDX-License-Identifier: GPL-3.0-or-later
#include "dreadovania/json.hpp"

#include <cmath>
#include <cstdlib>

namespace dreadovania::json {

const Value* Value::get(std::string_view key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    const Value* found = nullptr;
    for (const auto& [name, value] : members_) {
        if (name == key) {
            found = &value;
        }
    }
    return found;
}

std::optional<std::string> Value::string_at(std::string_view key) const {
    const Value* v = get(key);
    if (v == nullptr || !v->is_string()) {
        return std::nullopt;
    }
    return v->as_string();
}

std::optional<bool> Value::bool_at(std::string_view key) const {
    const Value* v = get(key);
    if (v == nullptr || v->type() != Type::Bool) {
        return std::nullopt;
    }
    return v->as_bool();
}

std::optional<std::int64_t> Value::int_at(std::string_view key) const {
    const Value* v = get(key);
    if (v == nullptr || v->type() != Type::Number) {
        return std::nullopt;
    }
    double n = v->as_number();
    if (n != std::floor(n) || std::fabs(n) > 9007199254740991.0) {  // 2^53 - 1
        return std::nullopt;
    }
    return static_cast<std::int64_t>(n);
}

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    ParseResult run() {
        ParseResult result;
        if (text_.size() > kMaxInput) {
            result.error = "response too large";
            return result;
        }
        Value value;
        skip_space();
        if (!parse_value(value, 0)) {
            result.error = error_.empty() ? "invalid JSON" : error_;
            return result;
        }
        skip_space();
        if (pos_ != text_.size()) {
            result.error = "unexpected text after JSON";
            return result;
        }
        result.value = std::move(value);
        return result;
    }

  private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;

    bool fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }

    void skip_space() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            return fail("invalid literal");
        }
        pos_ += word.size();
        return true;
    }

    bool parse_value(Value& out, std::size_t depth) {
        if (depth > kMaxDepth) {
            return fail("JSON nested too deeply");
        }
        if (pos_ >= text_.size()) {
            return fail("unexpected end of JSON");
        }
        switch (text_[pos_]) {
            case 'n':
                out.type_ = Value::Type::Null;
                return literal("null");
            case 't':
                out.type_ = Value::Type::Bool;
                out.bool_ = true;
                return literal("true");
            case 'f':
                out.type_ = Value::Type::Bool;
                out.bool_ = false;
                return literal("false");
            case '"':
                out.type_ = Value::Type::String;
                return parse_string(out.string_);
            case '[':
                return parse_array(out, depth);
            case '{':
                return parse_object(out, depth);
            default:
                return parse_number(out);
        }
    }

    bool parse_number(Value& out) {
        std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        auto digits = [&]() {
            std::size_t begin = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            return pos_ - begin;
        };
        if (pos_ < text_.size() && text_[pos_] == '0') {
            ++pos_;
        } else if (digits() == 0) {
            return fail("invalid number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (digits() == 0) {
                return fail("invalid number");
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (digits() == 0) {
                return fail("invalid number");
            }
        }
        std::string copy(text_.substr(start, pos_ - start));
        char* end = nullptr;
        double value = std::strtod(copy.c_str(), &end);
        if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
            return fail("invalid number");
        }
        out.type_ = Value::Type::Number;
        out.number_ = value;
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) {
            return fail("invalid escape");
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return fail("invalid escape");
            }
        }
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return fail("control character in string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                break;
            }
            char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!hex4(cp)) {
                        return false;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // a surrogate pair
                        std::uint32_t low = 0;
                        if (text_.substr(pos_, 2) != "\\u") {
                            return fail("invalid surrogate pair");
                        }
                        pos_ += 2;
                        if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            return fail("invalid surrogate pair");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("invalid surrogate pair");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return fail("invalid escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_array(Value& out, std::size_t depth) {
        out.type_ = Value::Type::Array;
        ++pos_;
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            Value item;
            skip_space();
            if (!parse_value(item, depth + 1)) {
                return false;
            }
            out.items_.push_back(std::move(item));
            skip_space();
            if (pos_ >= text_.size()) {
                return fail("unterminated array");
            }
            char c = text_[pos_++];
            if (c == ']') {
                return true;
            }
            if (c != ',') {
                return fail("expected , or ] in array");
            }
        }
    }

    bool parse_object(Value& out, std::size_t depth) {
        out.type_ = Value::Type::Object;
        ++pos_;
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected a key in object");
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_space();
            if (pos_ >= text_.size() || text_[pos_++] != ':') {
                return fail("expected : in object");
            }
            skip_space();
            Value value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.members_.emplace_back(std::move(key), std::move(value));
            skip_space();
            if (pos_ >= text_.size()) {
                return fail("unterminated object");
            }
            char c = text_[pos_++];
            if (c == '}') {
                return true;
            }
            if (c != ',') {
                return fail("expected , or } in object");
            }
        }
    }
};

ParseResult parse(std::string_view text) { return Parser(text).run(); }

std::string quote(std::string_view text) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (u < 0x20) {
                    out += "\\u00";
                    out += hex[u >> 4];
                    out += hex[u & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

}  // namespace dreadovania::json
