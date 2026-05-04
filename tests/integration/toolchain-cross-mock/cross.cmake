# Mock cross-toolchain: host clang with --target=riscv32, standing in for
# a real rv32 toolchain. Exercises kiln's toolchain-file plumbing.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET riscv32-unknown-none-elf)
