add_library(hakka_json_compiler_options INTERFACE)
target_compile_options(hakka_json_compiler_options INTERFACE
     $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
         -Wall -Wextra -Wpedantic -Werror>
     $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4702 /wd5030>
 )

