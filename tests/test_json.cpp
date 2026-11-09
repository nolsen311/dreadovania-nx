// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>

#include "check.hpp"
#include "dreadovania/json.hpp"

using dreadovania::json::parse;
using dreadovania::json::quote;
using Type = dreadovania::json::Value::Type;

TEST(json_values) {
    auto r = parse(R"( {"a": [1, -2.5e3, true, false, null], "b": {"c": "d"}} )");
    CHECK(r.error.empty());
    CHECK(r.value && r.value->is_object());
    const auto* a = r.value->get("a");
    CHECK(a && a->is_array() && a->items().size() == 5);
    CHECK(a->items()[1].as_number() == -2500.0);
    CHECK(a->items()[2].as_bool());
    CHECK(a->items()[4].is_null());
    CHECK(r.value->get("b")->string_at("c") == "d");
    CHECK(r.value->get("missing") == nullptr);
}

TEST(json_typed_lookups) {
    auto r = parse(R"({"n": 42, "f": 1.5, "s": "x", "big": 1e300})");
    CHECK(r.value->int_at("n") == 42);
    CHECK(!r.value->int_at("f"));
    CHECK(!r.value->int_at("s"));
    CHECK(!r.value->int_at("big"));
    CHECK(!r.value->string_at("n"));
    CHECK(r.value->string_at("s") == "x");
}

TEST(json_strings_and_escapes) {
    auto r = parse(R"(["a\"b\\c\/\n\t", "\u00e9\u20ac", "\ud83d\ude00"])");
    CHECK(r.error.empty());
    CHECK(r.value->items()[0].as_string() == "a\"b\\c/\n\t");
    CHECK(r.value->items()[1].as_string() == "\xc3\xa9\xe2\x82\xac");
    CHECK(r.value->items()[2].as_string() == "\xf0\x9f\x98\x80");
}

TEST(json_rejects_malformed_input) {
    const char* bad[] = {
        "", "{", "[1,]", "{\"a\" 1}", "{\"a\": 1,}", "01", "1.", "-", "+1", "nul",
        "\"abc", "\"a\x01\"", "\"\\x\"", "\"\\ud800\"", "\"\\udc00\"", "[1] 2", "NaN",
        "{1: 2}", "\"\\u12\"",
    };
    for (const char* text : bad) {
        auto r = parse(text);
        CHECK(!r.value);
        CHECK(!r.error.empty());
    }
}

TEST(json_limits_depth_and_size) {
    std::string deep(40, '[');
    deep += std::string(40, ']');
    CHECK(parse(deep).error == "JSON nested too deeply");
    std::string ok(20, '[');
    ok += std::string(20, ']');
    CHECK(parse(ok).value.has_value());
    std::string big = "\"" + std::string(dreadovania::json::kMaxInput, 'a') + "\"";
    CHECK(parse(big).error == "response too large");
}

TEST(json_quote_round_trips) {
    std::string tricky = "quote\" back\\ nl\n ctrl\x01 caf\xc3\xa9";
    auto r = parse(quote(tricky));
    CHECK(r.value && r.value->as_string() == tricky);
    CHECK(quote("\x01") == "\"\\u0001\"");
}

TEST(json_survives_corrupted_input) {
    // Mutations of a real response must never crash or read out of bounds
    // (the tests run under AddressSanitizer and UBSan).
    const std::string base =
        R"([{"id": "s1", "status": "ready", "mod": {"status": "ready"}, "n": -1.5e3,)"
        R"( "u": "é😀", "list": [true, false, null]}])";
    unsigned int state = 12345;
    auto next = [&]() {
        state = state * 1103515245u + 12345u;
        return state >> 16;
    };
    int parsed = 0;
    for (int round = 0; round < 20000; ++round) {
        std::string text = base;
        int edits = 1 + static_cast<int>(next() % 4);
        for (int i = 0; i < edits; ++i) {
            std::size_t at = next() % text.size();
            switch (next() % 3) {
                case 0: text[at] = static_cast<char>(next() % 256); break;
                case 1: text.erase(at, 1 + next() % 8); break;
                default: text.insert(at, 1, "{}[]\",:\\u0"[next() % 11]); break;
            }
            if (text.empty()) {
                text = "x";
            }
        }
        auto r = parse(text);
        CHECK(r.value.has_value() == r.error.empty());
        parsed += r.value ? 1 : 0;
    }
    CHECK(parsed < 20000);  // most mutations are invalid, and are reported as such
}
