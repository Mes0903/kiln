string(SHA256 "hello world" result)
message("SHA256('hello world') = ${result}")

if(result STREQUAL "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9")
    message("Test PASSED")
else()
    message(FATAL_ERROR "Test FAILED: Expected b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9, got ${result}")
endif()
