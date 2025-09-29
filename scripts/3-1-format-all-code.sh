find . -type f \( -name "*.c" -o -name "*.h" \) ! -name "VGAModes.c" ! -path "./third/*" -exec clang-format -i {} +
