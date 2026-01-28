cmake_minimum_required(VERSION 3.15)
project(file_strings_test)

# Create a test file with various content
file(WRITE "${CMAKE_BINARY_DIR}/test_data.txt" "Hello World\nThis is a test line\nShort\nThis is a longer line of text\n")

# Test 1: Basic string extraction
file(STRINGS "${CMAKE_BINARY_DIR}/test_data.txt" LINES)
message(STATUS "Test 1 - Basic extraction:")
foreach(line ${LINES})
    message(STATUS "  Line: ${line}")
endforeach()
list(LENGTH LINES line_count)
if(NOT line_count EQUAL 4)
    message(FATAL_ERROR "Expected 4 lines, got ${line_count}")
endif()

# Test 2: LENGTH_MINIMUM filter
file(STRINGS "${CMAKE_BINARY_DIR}/test_data.txt" LONG_LINES LENGTH_MINIMUM 20)
message(STATUS "Test 2 - LENGTH_MINIMUM 20:")
foreach(line ${LONG_LINES})
    message(STATUS "  Line: ${line}")
endforeach()
list(LENGTH LONG_LINES long_count)
if(NOT long_count EQUAL 1)
    message(FATAL_ERROR "Expected 1 long line, got ${long_count}")
endif()

# Test 3: LENGTH_MAXIMUM filter (should get "Hello World" and "Short")
file(STRINGS "${CMAKE_BINARY_DIR}/test_data.txt" SHORT_LINES LENGTH_MAXIMUM 11)
message(STATUS "Test 3 - LENGTH_MAXIMUM 11:")
foreach(line ${SHORT_LINES})
    message(STATUS "  Line: ${line}")
endforeach()
list(LENGTH SHORT_LINES short_count)
if(NOT short_count EQUAL 2)
    message(FATAL_ERROR "Expected 2 short lines, got ${short_count}")
endif()

# Test 4: LIMIT_COUNT
file(STRINGS "${CMAKE_BINARY_DIR}/test_data.txt" LIMITED_LINES LIMIT_COUNT 2)
message(STATUS "Test 4 - LIMIT_COUNT 2:")
foreach(line ${LIMITED_LINES})
    message(STATUS "  Line: ${line}")
endforeach()
list(LENGTH LIMITED_LINES limited_count)
if(NOT limited_count EQUAL 2)
    message(FATAL_ERROR "Expected 2 lines (limited), got ${limited_count}")
endif()

# Test 5: REGEX filter
file(STRINGS "${CMAKE_BINARY_DIR}/test_data.txt" TEST_LINES REGEX ".*test.*")
message(STATUS "Test 5 - REGEX '.*test.*':")
foreach(line ${TEST_LINES})
    message(STATUS "  Line: ${line}")
endforeach()
list(LENGTH TEST_LINES test_count)
if(NOT test_count EQUAL 1)
    message(FATAL_ERROR "Expected 1 line matching 'test', got ${test_count}")
endif()

# Test 6: File with non-printable characters (simulated)
# Note: file(WRITE) doesn't interpret escape sequences, so we'll test basic behavior
file(WRITE "${CMAKE_BINARY_DIR}/mixed_data.txt" "ValidString1\nValidString2")
file(STRINGS "${CMAKE_BINARY_DIR}/mixed_data.txt" MIXED_STRINGS)
message(STATUS "Test 6 - Mixed data:")
foreach(str ${MIXED_STRINGS})
    message(STATUS "  String: ${str}")
endforeach()
list(LENGTH MIXED_STRINGS mixed_count)
if(NOT mixed_count EQUAL 2)
    message(FATAL_ERROR "Expected 2 strings from mixed data, got ${mixed_count}")
endif()

# Test 7: NEWLINE_CONSUME
file(WRITE "${CMAKE_BINARY_DIR}/multiline.txt" "Line1\nLine2\nLine3")
file(STRINGS "${CMAKE_BINARY_DIR}/multiline.txt" CONSUMED_LINES NEWLINE_CONSUME)
message(STATUS "Test 7 - NEWLINE_CONSUME:")
foreach(line ${CONSUMED_LINES})
    message(STATUS "  Line: '${line}'")
endforeach()
# With NEWLINE_CONSUME, the whole file becomes one big string (with embedded newlines)
list(LENGTH CONSUMED_LINES consumed_count)
message(STATUS "  Got ${consumed_count} strings with NEWLINE_CONSUME")

# Test 8: Combined filters
file(STRINGS "${CMAKE_BINARY_DIR}/test_data.txt" COMBINED
    LENGTH_MINIMUM 10
    LENGTH_MAXIMUM 20
    LIMIT_COUNT 1)
message(STATUS "Test 8 - Combined filters (min=10, max=20, count=1):")
foreach(line ${COMBINED})
    message(STATUS "  Line: ${line}")
endforeach()
list(LENGTH COMBINED combined_count)
if(NOT combined_count EQUAL 1)
    message(FATAL_ERROR "Expected 1 line with combined filters, got ${combined_count}")
endif()

message(STATUS "All file(STRINGS) tests passed!")
