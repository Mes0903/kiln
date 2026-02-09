# Test IS_X86_64_ARCH detection
message(STATUS "CMAKE_SYSTEM_PROCESSOR = ${CMAKE_SYSTEM_PROCESSOR}")

if(NOT DEFINED IS_X86_64_ARCH AND ${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64|AMD64")
  set(IS_X86_64_ARCH TRUE)
else()
  set(IS_X86_64_ARCH FALSE)
endif()

message(STATUS "IS_X86_64_ARCH = ${IS_X86_64_ARCH}")
message(STATUS "MSVC = ${MSVC}")

if (IS_X86_64_ARCH AND NOT MSVC)
    message(STATUS "Would call folly_add_library")
else()
    message(STATUS "Would SKIP folly_add_library")
endif()
