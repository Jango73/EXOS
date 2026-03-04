find . -type f \( -name "*.c" -o -name "*.h" \) ! -name "VGA-Modes.c" ! -path "./third/*" -exec clang-format -i {} +
