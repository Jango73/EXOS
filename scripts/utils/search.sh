#!/usr/bin/env sh
# search.sh
# Recursively scan files by extension and search for a string (partial or whole-word match).
# Output format: <FileName>:<LineNumber>\t<LineContent>

set -eu

print_help() {
    cat <<'EOF'
Usage:
  search.sh -e <extension> -s <string> [-m partial|whole] [-p <path>]

Options:
  -e <extension>   File extension to scan (e.g. "js" or ".js")
  -s <string>      String to search for (literal)
  -m <mode>        Match mode: "partial" (default) or "whole"
  -p <path>        Root path to scan (default: current directory)
  -h               Show help

Examples:
  ./search.sh -e js -s "TODO"
  ./search.sh -e ".ts" -s "Player" -m whole -p ./src
EOF
}

EXT=""
NEEDLE=""
MODE="partial"
ROOT="."

while getopts "e:s:m:p:h" opt; do
    case "$opt" in
        e) EXT="$OPTARG" ;;
        s) NEEDLE="$OPTARG" ;;
        m) MODE="$OPTARG" ;;
        p) ROOT="$OPTARG" ;;
        h) print_help; exit 0 ;;
        *) print_help; exit 2 ;;
    esac
done

if [ -z "$EXT" ] || [ -z "$NEEDLE" ]; then
    print_help
    exit 2
fi

# Normalize extension: accept "js" or ".js"
case "$EXT" in
    .*) EXT="${EXT#.}" ;;
esac

case "$MODE" in
    partial|whole) : ;;
    *) echo "Error: -m must be 'partial' or 'whole'." >&2; exit 2 ;;
esac

if [ ! -d "$ROOT" ]; then
    echo "Error: path does not exist or is not a directory: $ROOT" >&2
    exit 2
fi

escape_regex() {
    # Escape regex special characters for awk
    printf '%s' "$1" | sed 's/[][(){}.*+?^$|\\]/\\&/g'
}

ESCAPED_NEEDLE="$(escape_regex "$NEEDLE")"

if [ "$MODE" = "whole" ]; then
    AWK_SCRIPT='
        BEGIN {
            re = "(^|[^[:alnum:]_])" needle "([^[:alnum:]_]|$)"
        }
        {
            if ($0 ~ re) {
                printf("%s:%d\t%s\n", FILENAME, FNR, $0)
            }
        }
    '
else
    AWK_SCRIPT='
        {
            if (index($0, needle) > 0) {
                printf("%s:%d\t%s\n", FILENAME, FNR, $0)
            }
        }
    '
fi

# Use find with -exec to stay POSIX-compliant (no read -d, no -print0)
find "$ROOT" -type f -name "*.${EXT}" -exec awk -v needle="$ESCAPED_NEEDLE" "$AWK_SCRIPT" {} +
