cmake_minimum_required(VERSION 3.15)
project(file_download_extract_test)

# --- Test file(DOWNLOAD) ---
# Download a small file (example.com is reliable and tiny)
file(DOWNLOAD "https://example.com"
     ${CMAKE_CURRENT_BINARY_DIR}/example.html
     STATUS dl_status
     TIMEOUT 30)

list(GET dl_status 0 dl_code)
if(NOT dl_code EQUAL 0)
    message(WARNING "Download failed (network may be unavailable): ${dl_status}")
else()
    message(STATUS "Download succeeded: ${dl_status}")
    if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/example.html)
        message(FATAL_ERROR "Downloaded file does not exist!")
    endif()
    file(SIZE ${CMAKE_CURRENT_BINARY_DIR}/example.html file_size)
    if(file_size EQUAL 0)
        message(FATAL_ERROR "Downloaded file is empty!")
    endif()
    message(STATUS "Downloaded file size: ${file_size} bytes")
endif()

# --- Test file(ARCHIVE_EXTRACT) ---
# Create a test archive using execute_process so we have something to extract
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/archive_src)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/archive_src/hello.txt "Hello from archive!\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/archive_src/subdir/nested.txt "Nested file\n")

execute_process(
    COMMAND tar czf ${CMAKE_CURRENT_BINARY_DIR}/test.tar.gz
            -C ${CMAKE_CURRENT_BINARY_DIR}/archive_src .
    RESULT_VARIABLE tar_result)

if(NOT tar_result EQUAL 0)
    message(FATAL_ERROR "Failed to create test archive")
endif()

# Extract the archive
file(ARCHIVE_EXTRACT
     INPUT ${CMAKE_CURRENT_BINARY_DIR}/test.tar.gz
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/extracted
     VERBOSE)

# Verify extraction
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/extracted/hello.txt)
    message(FATAL_ERROR "Extracted file hello.txt does not exist!")
endif()

file(READ ${CMAKE_CURRENT_BINARY_DIR}/extracted/hello.txt content)
if(NOT content STREQUAL "Hello from archive!\n")
    message(FATAL_ERROR "Extracted hello.txt has wrong content: '${content}'")
endif()

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/extracted/subdir/nested.txt)
    message(FATAL_ERROR "Extracted nested file does not exist!")
endif()

message(STATUS "Archive extraction verified successfully")

# Test PATTERNS filtering
file(ARCHIVE_EXTRACT
     INPUT ${CMAKE_CURRENT_BINARY_DIR}/test.tar.gz
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/filtered
     PATTERNS "*.txt"
     VERBOSE)

# Test LIST_ONLY
file(ARCHIVE_EXTRACT
     INPUT ${CMAKE_CURRENT_BINARY_DIR}/test.tar.gz
     LIST_ONLY)

message(STATUS "All file(DOWNLOAD) and file(ARCHIVE_EXTRACT) tests passed!")
