#include <catch2/catch_test_macros.hpp>
#include "kiln/utils.hpp"

using namespace kiln;

TEST_CASE("compare_versions: equal versions", "[version]") {
    CHECK(compare_versions("3.16", "3.16") == 0);
    CHECK(compare_versions("1.0.0", "1.0.0") == 0);
    CHECK(compare_versions("0", "0") == 0);
    CHECK(compare_versions("3.22.0", "3.22.0") == 0);
}

TEST_CASE("compare_versions: less than", "[version]") {
    CHECK(compare_versions("2.8", "3.16") < 0);
    CHECK(compare_versions("3.15", "3.16") < 0);
    CHECK(compare_versions("3.16", "3.16.1") < 0);
    CHECK(compare_versions("3.16.0", "3.16.1") < 0);
    CHECK(compare_versions("1.0", "2.0") < 0);
}

TEST_CASE("compare_versions: greater than", "[version]") {
    CHECK(compare_versions("4.0", "3.22") > 0);
    CHECK(compare_versions("3.22", "3.16") > 0);
    CHECK(compare_versions("3.16.1", "3.16") > 0);
    CHECK(compare_versions("3.16.1", "3.16.0") > 0);
    CHECK(compare_versions("10.0", "9.99") > 0);
}

TEST_CASE("compare_versions: missing components treated as zero", "[version]") {
    CHECK(compare_versions("3.16", "3.16.0") == 0);
    CHECK(compare_versions("3.16.0", "3.16") == 0);
    CHECK(compare_versions("3", "3.0.0") == 0);
    CHECK(compare_versions("3.0.0.0", "3") == 0);
}

TEST_CASE("compare_versions: non-numeric suffix stripped", "[version]") {
    CHECK(compare_versions("1a", "1") == 0);
    CHECK(compare_versions("3.16-rc1", "3.16") == 0);
    CHECK(compare_versions("2.8.12a", "2.8.12") == 0);
    CHECK(compare_versions("1a", "2") < 0);
    CHECK(compare_versions("2-beta", "1") > 0);
}

TEST_CASE("compare_versions: empty strings", "[version]") {
    CHECK(compare_versions("", "") == 0);
    CHECK(compare_versions("1.0", "") > 0);
    CHECK(compare_versions("", "1.0") < 0);
}

TEST_CASE("compare_versions: numeric comparison not lexicographic", "[version]") {
    // "9" > "10" lexicographically, but 9 < 10 numerically
    CHECK(compare_versions("9", "10") < 0);
    CHECK(compare_versions("1.9", "1.10") < 0);
    CHECK(compare_versions("1.2.9", "1.2.10") < 0);
}
