#include "pch.h"
// test_helpers.cpp
// Unit tests for inline helpers declared in helpers.h:
//   split, join, starts_with, to_upper, trim, freqToAnnotation, round_to_closest

#include <doctest/doctest.h>
#include "helpers.h"

// ─── split ───────────────────────────────────────────────────────────────────

TEST_CASE("split - space delimiter produces correct tokens")
{
    auto v = split("a b c");
    REQUIRE(v.size() == 3);
    CHECK(v[0] == "a");
    CHECK(v[1] == "b");
    CHECK(v[2] == "c");
}

TEST_CASE("split - custom delimiter")
{
    auto v = split("one,two,three", ',');
    REQUIRE(v.size() == 3);
    CHECK(v[0] == "one");
    CHECK(v[1] == "two");
    CHECK(v[2] == "three");
}

TEST_CASE("split - empty string produces one empty token")
{
    // std::getline always produces at least one token for an empty string
    auto v = split("");
    CHECK(v.size() == 1);
    CHECK(v[0] == "");
}

TEST_CASE("split - single token with no delimiter")
{
    auto v = split("hello");
    REQUIRE(v.size() == 1);
    CHECK(v[0] == "hello");
}

// ─── join ────────────────────────────────────────────────────────────────────

TEST_CASE("join - basic space join (trailing delimiter)")
{
    // join appends the delimiter after every element including the last
    std::string result = join({"a", "b", "c"});
    CHECK(result == "a b c ");
}

TEST_CASE("join - custom delimiter")
{
    std::string result = join({"x", "y"}, ',');
    CHECK(result == "x,y,");
}

TEST_CASE("join - empty vector produces empty string")
{
    CHECK(join({}) == "");
}

TEST_CASE("join - single element produces element + delimiter")
{
    CHECK(join({"only"}) == "only ");
}

// ─── starts_with ─────────────────────────────────────────────────────────────

TEST_CASE("starts_with - matching prefix returns true")
{
    CHECK(starts_with("hello world", "hello") == true);
}

TEST_CASE("starts_with - non-matching prefix returns false")
{
    CHECK(starts_with("hello world", "world") == false);
}

TEST_CASE("starts_with - empty prefix always returns true")
{
    CHECK(starts_with("anything", "") == true);
    CHECK(starts_with("", "") == true);
}

TEST_CASE("starts_with - empty string with non-empty prefix returns false")
{
    CHECK(starts_with("", "x") == false);
}

// ─── to_upper ────────────────────────────────────────────────────────────────

TEST_CASE("to_upper - converts lower-case to upper-case in place")
{
    std::string s = "hello";
    to_upper(s);
    CHECK(s == "HELLO");
}

TEST_CASE("to_upper - mixed case string")
{
    std::string s = "FlowX";
    to_upper(s);
    CHECK(s == "FLOWX");
}

TEST_CASE("to_upper - already upper-case is unchanged")
{
    std::string s = "ABC";
    to_upper(s);
    CHECK(s == "ABC");
}

TEST_CASE("to_upper - empty string does not crash")
{
    std::string s = "";
    CHECK_NOTHROW(to_upper(s));
    CHECK(s == "");
}

// ─── trim ────────────────────────────────────────────────────────────────────

TEST_CASE("trim - strips leading and trailing spaces")
{
    CHECK(trim("  hello  ") == "hello");
}

TEST_CASE("trim - no whitespace returns unchanged string")
{
    CHECK(trim("hello") == "hello");
}

TEST_CASE("trim - all whitespace returns empty string")
{
    CHECK(trim("   \t  ") == "");
}

TEST_CASE("trim - empty string returns empty string")
{
    CHECK(trim("") == "");
}

TEST_CASE("trim - custom charset")
{
    CHECK(trim("***hello***", "*") == "hello");
}

// ─── freqToAnnotation ────────────────────────────────────────────────────────

TEST_CASE("freqToAnnotation(double) - removes dot and pads to 6 chars")
{
    CHECK(freqToAnnotation(121.5) == "121500");
    CHECK(freqToAnnotation(121.6) == "121600");
    CHECK(freqToAnnotation(122.8) == "122800");
}

TEST_CASE("freqToAnnotation(double) - 3 decimal digits already precise")
{
    CHECK(freqToAnnotation(119.775) == "119775");
}

TEST_CASE("freqToAnnotation(string) - delegates to double overload")
{
    CHECK(freqToAnnotation(std::string("121.500")) == "121500");
    CHECK(freqToAnnotation(std::string("119.775")) == "119775");
}

TEST_CASE("freqToAnnotation(string) - short string like '121.6' padded correctly")
{
    // std::stod("121.6") → 121.6 → std::format("{:.3f}", ...) → "121.600" → "121600"
    CHECK(freqToAnnotation(std::string("121.6")) == "121600");
}

// ─── round_to_closest ────────────────────────────────────────────────────────

TEST_CASE("round_to_closest - rounds to nearest multiple of 5")
{
    CHECK(round_to_closest(7,  5) == 5);
    CHECK(round_to_closest(8,  5) == 10);
    CHECK(round_to_closest(10, 5) == 10);
    CHECK(round_to_closest(3,  5) == 5);
    CHECK(round_to_closest(2,  5) == 0);
}

TEST_CASE("round_to_closest - rounds to nearest multiple of 10")
{
    CHECK(round_to_closest(14, 10) == 10);
    CHECK(round_to_closest(15, 10) == 20);
    CHECK(round_to_closest(25, 10) == 30);
}

TEST_CASE("round_to_closest - closest=1 returns num unchanged")
{
    CHECK(round_to_closest(7, 1) == 7);
}
