#include <catch2/catch_test_macros.hpp>
#include "dmake/shadow_map.hpp"

using namespace dmake;

TEST_CASE("ShadowMap basic operations", "[shadow_map]") {
    ShadowMap map;

    SECTION("Initial state") {
        REQUIRE(map.depth() == 0);
        REQUIRE(map.get("VAR") == "");
        REQUIRE_FALSE(map.is_defined("VAR"));
    }

    SECTION("Set and get at root level") {
        map.set("VAR", "value1");
        REQUIRE(map.get("VAR") == "value1");
        REQUIRE(map.is_defined("VAR"));
    }

    SECTION("Modify existing variable at same depth") {
        map.set("VAR", "value1");
        map.set("VAR", "value2");
        REQUIRE(map.get("VAR") == "value2");
        REQUIRE(map.is_defined("VAR"));
    }

    SECTION("Multiple variables at root") {
        map.set("VAR1", "value1");
        map.set("VAR2", "value2");
        map.set("VAR3", "value3");

        REQUIRE(map.get("VAR1") == "value1");
        REQUIRE(map.get("VAR2") == "value2");
        REQUIRE(map.get("VAR3") == "value3");
    }
}

TEST_CASE("ShadowMap variable shadowing", "[shadow_map]") {
    ShadowMap map;

    SECTION("Shadow variable in nested scope") {
        map.set("VAR", "outer");
        REQUIRE(map.get("VAR") == "outer");

        map.push_scope();
        REQUIRE(map.depth() == 1);
        REQUIRE(map.get("VAR") == "outer");  // Parent value visible

        map.set("VAR", "inner");
        REQUIRE(map.get("VAR") == "inner");  // Shadowed

        map.pop_scope();
        REQUIRE(map.depth() == 0);
        REQUIRE(map.get("VAR") == "outer");  // Original value restored
    }

    SECTION("Multiple levels of shadowing") {
        map.set("VAR", "depth0");

        map.push_scope();  // depth 1
        map.set("VAR", "depth1");
        REQUIRE(map.get("VAR") == "depth1");

        map.push_scope();  // depth 2
        map.set("VAR", "depth2");
        REQUIRE(map.get("VAR") == "depth2");

        map.push_scope();  // depth 3
        map.set("VAR", "depth3");
        REQUIRE(map.get("VAR") == "depth3");

        map.pop_scope();  // back to depth 2
        REQUIRE(map.get("VAR") == "depth2");

        map.pop_scope();  // back to depth 1
        REQUIRE(map.get("VAR") == "depth1");

        map.pop_scope();  // back to depth 0
        REQUIRE(map.get("VAR") == "depth0");
    }

    SECTION("Shadow some variables but not others") {
        map.set("VAR1", "outer1");
        map.set("VAR2", "outer2");

        map.push_scope();
        map.set("VAR1", "inner1");  // Shadow VAR1
        // Don't shadow VAR2

        REQUIRE(map.get("VAR1") == "inner1");
        REQUIRE(map.get("VAR2") == "outer2");  // Still visible

        map.pop_scope();
        REQUIRE(map.get("VAR1") == "outer1");
        REQUIRE(map.get("VAR2") == "outer2");
    }
}

TEST_CASE("ShadowMap scope cleanup", "[shadow_map]") {
    ShadowMap map;

    SECTION("Pop scope removes all modifications") {
        map.push_scope();
        map.set("VAR1", "value1");
        map.set("VAR2", "value2");
        map.set("VAR3", "value3");

        REQUIRE(map.is_defined("VAR1"));
        REQUIRE(map.is_defined("VAR2"));
        REQUIRE(map.is_defined("VAR3"));

        map.pop_scope();

        REQUIRE_FALSE(map.is_defined("VAR1"));
        REQUIRE_FALSE(map.is_defined("VAR2"));
        REQUIRE_FALSE(map.is_defined("VAR3"));
    }

    SECTION("Pop scope only removes current depth") {
        map.set("VAR", "outer");

        map.push_scope();
        map.set("VAR", "inner");
        map.set("VAR2", "inner_only");

        map.pop_scope();

        REQUIRE(map.get("VAR") == "outer");  // Restored
        REQUIRE_FALSE(map.is_defined("VAR2"));  // Gone
    }

    SECTION("Empty scopes don't cause issues") {
        map.set("VAR", "value");

        map.push_scope();  // Don't modify anything
        map.pop_scope();

        REQUIRE(map.get("VAR") == "value");  // Unchanged
    }
}

TEST_CASE("ShadowMap unset behavior", "[shadow_map]") {
    ShadowMap map;

    SECTION("Unset at root removes variable") {
        map.set("VAR", "value");
        REQUIRE(map.is_defined("VAR"));

        map.unset("VAR");
        REQUIRE_FALSE(map.is_defined("VAR"));
        REQUIRE(map.get("VAR") == "");
    }

    SECTION("Unset in nested scope reveals parent value") {
        map.set("VAR", "outer");

        map.push_scope();
        map.set("VAR", "inner");
        REQUIRE(map.get("VAR") == "inner");

        map.unset("VAR");
        REQUIRE(map.get("VAR") == "outer");  // Parent visible again
        REQUIRE(map.is_defined("VAR"));
    }

    SECTION("Unset non-existent variable is safe") {
        map.unset("NONEXISTENT");  // Should not crash
        REQUIRE_FALSE(map.is_defined("NONEXISTENT"));
    }

    SECTION("Unset only affects current depth") {
        map.set("VAR", "depth0");

        map.push_scope();
        map.set("VAR", "depth1");

        map.push_scope();
        map.set("VAR", "depth2");
        map.unset("VAR");  // Remove depth2 version

        REQUIRE(map.get("VAR") == "depth1");  // depth1 now visible

        map.pop_scope();
        REQUIRE(map.get("VAR") == "depth1");  // Still there

        map.pop_scope();
        REQUIRE(map.get("VAR") == "depth0");  // Back to root
    }
}

TEST_CASE("ShadowMap PARENT_SCOPE", "[shadow_map]") {
    ShadowMap map;

    SECTION("Set parent scope from nested scope") {
        map.set("VAR", "original");

        map.push_scope();
        auto result = map.set_parent_scope("VAR", "modified_parent");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == true);  // Replaced existing

        // Parent value is modified, but we're still in child scope
        // The child scope doesn't have its own value, so parent is visible
        REQUIRE(map.get("VAR") == "modified_parent");

        map.pop_scope();
        REQUIRE(map.get("VAR") == "modified_parent");  // Parent was modified
    }

    SECTION("Set parent scope creates new variable in parent") {
        map.push_scope();
        auto result = map.set_parent_scope("NEW_VAR", "parent_value");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == false);  // Created new

        // We're in child scope, parent value is visible
        REQUIRE(map.get("NEW_VAR") == "parent_value");

        map.pop_scope();
        REQUIRE(map.get("NEW_VAR") == "parent_value");  // Exists in parent
    }

    SECTION("Set parent scope with local shadow") {
        map.set("VAR", "original");

        map.push_scope();
        map.set("VAR", "local");  // Shadow it locally
        auto result = map.set_parent_scope("VAR", "modified_parent");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == true);  // Replaced existing

        // Local value takes precedence
        REQUIRE(map.get("VAR") == "local");

        map.pop_scope();
        REQUIRE(map.get("VAR") == "modified_parent");  // Parent was modified
    }

    SECTION("Set parent scope at root returns error") {
        auto result = map.set_parent_scope("VAR", "value");
        REQUIRE_FALSE(result.has_value());  // Error - no parent scope
        REQUIRE_FALSE(map.is_defined("VAR"));
    }

    SECTION("Set parent scope multiple levels deep") {
        map.set("VAR", "depth0");

        map.push_scope();  // depth 1
        map.set("VAR", "depth1");

        map.push_scope();  // depth 2
        auto result = map.set_parent_scope("VAR", "modified_depth1");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == true);  // Replaced existing

        REQUIRE(map.get("VAR") == "modified_depth1");  // depth1 was modified

        map.pop_scope();  // back to depth 1
        REQUIRE(map.get("VAR") == "modified_depth1");  // depth1 has new value

        map.pop_scope();  // back to depth 0
        REQUIRE(map.get("VAR") == "depth0");  // depth0 unchanged
    }
}

TEST_CASE("ShadowMap deep nesting", "[shadow_map]") {
    ShadowMap map;

    SECTION("10 levels of nesting") {
        const int DEPTH = 10;

        // Set at each depth
        for (int i = 0; i < DEPTH; ++i) {
            map.push_scope();
            map.set("VAR", "value" + std::to_string(i));
            REQUIRE(map.depth() == i + 1);
        }

        REQUIRE(map.get("VAR") == "value9");

        // Pop back to root
        for (int i = DEPTH - 1; i >= 0; --i) {
            map.pop_scope();
            if (i > 0) {
                REQUIRE(map.get("VAR") == "value" + std::to_string(i - 1));
            }
        }

        REQUIRE(map.depth() == 0);
        REQUIRE_FALSE(map.is_defined("VAR"));
    }

    SECTION("Many variables at many depths") {
        for (int depth = 0; depth < 5; ++depth) {
            map.push_scope();
            for (int var = 0; var < 10; ++var) {
                std::string name = "VAR" + std::to_string(var);
                std::string value = "d" + std::to_string(depth) + "_v" + std::to_string(var);
                map.set(name, value);
            }
        }

        // Verify all variables at depth 5
        for (int var = 0; var < 10; ++var) {
            std::string name = "VAR" + std::to_string(var);
            std::string expected = "d4_v" + std::to_string(var);
            REQUIRE(map.get(name) == expected);
        }

        // Pop all scopes
        for (int depth = 0; depth < 5; ++depth) {
            map.pop_scope();
        }

        // All variables should be gone
        for (int var = 0; var < 10; ++var) {
            std::string name = "VAR" + std::to_string(var);
            REQUIRE_FALSE(map.is_defined(name));
        }
    }
}

TEST_CASE("ShadowMap mixed operations", "[shadow_map]") {
    ShadowMap map;

    SECTION("Complex scenario with shadowing, unset, and parent scope") {
        // Root level
        map.set("VAR1", "root1");
        map.set("VAR2", "root2");

        // Level 1
        map.push_scope();
        map.set("VAR1", "level1_1");  // Shadow VAR1
        map.set("VAR3", "level1_3");  // New variable

        // Level 2
        map.push_scope();
        map.set("VAR2", "level2_2");  // Shadow VAR2
        auto result = map.set_parent_scope("VAR1", "modified_level1_1");  // Modify parent's VAR1
        REQUIRE(result.has_value());
        REQUIRE(result.value() == true);  // Replaced existing

        REQUIRE(map.get("VAR1") == "modified_level1_1");  // Parent was modified
        REQUIRE(map.get("VAR2") == "level2_2");  // Shadowed
        REQUIRE(map.get("VAR3") == "level1_3");  // From parent

        map.unset("VAR2");  // Remove shadow
        REQUIRE(map.get("VAR2") == "root2");  // Root value visible

        map.pop_scope();  // Back to level 1

        REQUIRE(map.get("VAR1") == "modified_level1_1");  // Was modified by child
        REQUIRE(map.get("VAR2") == "root2");  // From root
        REQUIRE(map.get("VAR3") == "level1_3");  // Local

        map.pop_scope();  // Back to root

        REQUIRE(map.get("VAR1") == "root1");  // Original root value
        REQUIRE(map.get("VAR2") == "root2");  // Original root value
        REQUIRE_FALSE(map.is_defined("VAR3"));  // Was only at level 1
    }
}

TEST_CASE("ShadowMap modification at same depth", "[shadow_map]") {
    ShadowMap map;

    SECTION("Multiple modifications at same depth don't create duplicates") {
        map.set("VAR", "value1");
        map.set("VAR", "value2");
        map.set("VAR", "value3");

        REQUIRE(map.get("VAR") == "value3");

        // Should only have one version at depth 0
        map.push_scope();
        REQUIRE(map.get("VAR") == "value3");

        map.set("VAR", "nested");
        map.pop_scope();

        REQUIRE(map.get("VAR") == "value3");  // Only one version at root
    }
}

TEST_CASE("ShadowMap edge cases", "[shadow_map]") {
    ShadowMap map;

    SECTION("Empty variable values") {
        map.set("VAR", "");
        REQUIRE(map.is_defined("VAR"));
        REQUIRE(map.get("VAR") == "");
    }

    SECTION("Variable names with special characters") {
        map.set("CMAKE_CXX_FLAGS", "-std=c++23");
        map.set("MY_VAR_123", "value");
        map.set("_UNDERSCORE", "test");

        REQUIRE(map.get("CMAKE_CXX_FLAGS") == "-std=c++23");
        REQUIRE(map.get("MY_VAR_123") == "value");
        REQUIRE(map.get("_UNDERSCORE") == "test");
    }

    SECTION("Pop scope at root is safe") {
        map.pop_scope();  // Should not crash
        REQUIRE(map.depth() == 0);
    }

    SECTION("Multiple push/pop cycles") {
        for (int i = 0; i < 5; ++i) {
            map.push_scope();
            map.set("VAR", "cycle" + std::to_string(i));
            REQUIRE(map.get("VAR") == "cycle" + std::to_string(i));
            map.pop_scope();
            REQUIRE_FALSE(map.is_defined("VAR"));
        }
    }
}
