project(ToolchainCrossMock C)
message(STATUS "C compiler ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "sizeof(void*): ${CMAKE_SIZEOF_VOID_P}")
message(STATUS "Compiler target: ${CMAKE_C_COMPILER_TARGET}")

# Freestanding object — proves the cross compiler is actually invoked
# with --target= and produces a 32-bit RISC-V object, even though no
# real cross toolchain is installed (we use host clang's built-in
# riscv32 backend as the stand-in).
add_library(rv32obj OBJECT freestanding.c)
target_compile_options(rv32obj PRIVATE -ffreestanding -nostdlib)
