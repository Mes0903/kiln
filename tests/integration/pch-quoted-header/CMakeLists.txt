project(pch_quoted_header)

# Header name is passed with literal surrounding quotes — this is the form
# LLVM emits via add_llvm_library's PRECOMPILE_HEADERS keyword
# (e.g. PRECOMPILE_HEADERS [["llvm/CodeGen/pch.h"]]).
# Kiln must strip the quotes when writing the include directive and must
# not treat the quoted form as a literal source-relative file path.
add_executable(app main.cpp)
target_precompile_headers(app PRIVATE [["pch.hpp"]])
