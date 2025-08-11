find . -type f \( -name "*.c" -o -name "*.h" \) ! -name "VGAModes.c" -exec clang-format -i {} +
