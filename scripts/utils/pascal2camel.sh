#!/usr/bin/env bash
# Recursively convert PascalCase → camelCase in files matching given extensions.
# Ignores content inside "..." and '...', C/JS comments (//, /*...*/), and ASM '; ...' (for .asm/.s/.S files only).
# Handles BIOSMark → biosMark logic.
# Usage: ./pascal2camel.sh js ts c h cpp asm s

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "Usage: $0 <ext1> [ext2] ..." >&2
    exit 1
fi

# Build find argument list safely
args=( . -type f "(" )
first=1
for ext in "$@"; do
    [ -z "$ext" ] && continue
    if [ $first -eq 0 ]; then args+=( -o ); fi
    args+=( -name "*.${ext}" )
    first=0
done
args+=( ")" -print0 )

while IFS= read -r -d '' file; do
    case "${file##*.}" in
        asm|ASM|s|S) ASM_FLAG=1 ;;
        *) ASM_FLAG=0 ;;
    esac

    ASM="$ASM_FLAG" perl -0777 -i -pe '
        my $is_asm = $ENV{"ASM"} || 0;

        # Pattern for ignored segments
        my $pat = $is_asm
            ? qr/( "(?:[^"\\]|\\.)*" | \x27(?:[^\\\x27]|\\.)*\x27 | \/\/[^\r\n]* | \/\*.*?\*\/ | ;[^\r\n]* )/sx
            : qr/( "(?:[^"\\]|\\.)*" | \x27(?:[^\\\x27]|\\.)*\x27 | \/\/[^\r\n]* | \/\*.*?\*\/ )/sx;

        my @seg = split $pat, $_, -1;

        for (my $i = 0; $i < @seg; $i++) {
            my $s = $seg[$i];
            next unless defined $s && length $s;
            if ($s =~ /\A(?:"|\x27|\/\*|\/\/|;)/) { next; }

            # Replacement rule: handle prefixes like BIOSMark → biosMark
            $s =~ s/\b([A-Z]+)([A-Z][a-z][A-Za-z0-9]*)\b/
                lc($1) . ucfirst($2)
            /eg;

            # Classic PascalCase → camelCase fallback
            $s =~ s/\b([A-Z])([a-z0-9]+(?:[A-Z][a-z0-9]+)+)\b/
                lc($1) . $2
            /eg;

            $seg[$i] = $s;
        }

        $_ = join("", @seg);
    ' "$file"

    echo "[updated] $file"
done < <(find "${args[@]}")
