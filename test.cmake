project(test C CXX)

get_property(_langs GLOBAL PROPERTY ENABLED_LANGUAGES)
message("Before: ENABLED_LANGUAGES = '${_langs}'")

include(CheckCXXSourceCompiles)

function(test_func)
  get_property(_langs GLOBAL PROPERTY ENABLED_LANGUAGES)
  message("In function: ENABLED_LANGUAGES = '${_langs}'")

  CHECK_CXX_SOURCE_COMPILES("int main() { return 0; }" TEST_VAR)
  message("TEST_VAR = ${TEST_VAR}")
endfunction()

test_func()
