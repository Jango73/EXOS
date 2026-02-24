#!/bin/sh

# Scan tracked .c/.h files, report files above a configurable line limit,
# and ensure each file starts with the standard source header.
LIMIT="${1:-1000}"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HEADER_TEMPLATE="$SCRIPT_DIR/../../documentation/Code-Header.txt"
EXCEPTIONS_FILE="$SCRIPT_DIR/check-code-exceptions.txt"
EXCEPTIONS_ALL=$(mktemp)
EXCEPTIONS_HEADER=$(mktemp)
EXCEPTIONS_LINE_LIMIT=$(mktemp)

if ! printf '%s' "$LIMIT" | grep -Eq '^[0-9]+$'; then
    echo "ERROR: limit must be an integer"
    echo "Usage: $0 [line_limit]"
    exit 2
fi

if [ ! -f "$HEADER_TEMPLATE" ]; then
    echo "ERROR: missing header template: $HEADER_TEMPLATE"
    exit 2
fi

if command -v git >/dev/null 2>&1; then
    FILES=$(git ls-files '*.c' '*.h' ':(exclude)third/**' ':(exclude)documentation/**')
    SCAN_MODE="tracked .c/.h files (excluding third/ and documentation/)"
else
    FILES=$(find . \
        -path './.git' -prune -o \
        -path './third' -prune -o \
        -path './documentation' -prune -o \
        -type f \( -name '*.c' -o -name '*.h' \) -print | sed 's|^\./||')
    SCAN_MODE="filesystem .c/.h files (excluding .git/, third/ and documentation/)"
fi

if [ -z "$FILES" ]; then
    echo "No .c or .h files found."
    exit 0
fi

TOTAL_FILES=0
EXCEEDED_COUNT=0
HEADER_MISSING_COUNT=0
EXCEEDED_LIST=$(mktemp)
HEADER_MISSING_LIST=$(mktemp)

cleanup() {
    rm -f "$EXCEEDED_LIST" \
        "$HEADER_MISSING_LIST" \
        "$EXCEPTIONS_ALL" \
        "$EXCEPTIONS_HEADER" \
        "$EXCEPTIONS_LINE_LIMIT"
}
trap cleanup EXIT

load_exceptions() {
    if [ ! -f "$EXCEPTIONS_FILE" ]; then
        return 0
    fi

    while IFS= read -r Line || [ -n "$Line" ]; do
        # Normalize spaces at start/end.
        Line=$(printf '%s' "$Line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        if [ -z "$Line" ]; then
            continue
        fi
        case "$Line" in
            \#*)
                continue
                ;;
        esac

        # Format: <path> [all|header|line-limit]
        set -- $Line
        ExceptionPath=$1
        ExceptionScope=$2

        case "$ExceptionScope" in
            ""|"all")
                printf '%s\n' "$ExceptionPath" >> "$EXCEPTIONS_ALL"
                ;;
            "header")
                printf '%s\n' "$ExceptionPath" >> "$EXCEPTIONS_HEADER"
                ;;
            "line-limit")
                printf '%s\n' "$ExceptionPath" >> "$EXCEPTIONS_LINE_LIMIT"
                ;;
            *)
                echo "WARNING: invalid exception scope in $EXCEPTIONS_FILE: $Line"
                ;;
        esac
    done < "$EXCEPTIONS_FILE"
}

is_exception() {
    ExceptionFilePath=$1
    ExceptionCheckType=$2

    if grep -Fxq "$ExceptionFilePath" "$EXCEPTIONS_ALL"; then
        return 0
    fi

    if [ "$ExceptionCheckType" = "header" ] &&
        grep -Fxq "$ExceptionFilePath" "$EXCEPTIONS_HEADER"; then
        return 0
    fi

    if [ "$ExceptionCheckType" = "line-limit" ] &&
        grep -Fxq "$ExceptionFilePath" "$EXCEPTIONS_LINE_LIMIT"; then
        return 0
    fi

    return 1
}

load_exceptions

printf 'Line limit: %s\n' "$LIMIT"
printf 'Header template: %s\n' "$HEADER_TEMPLATE"
if [ -f "$EXCEPTIONS_FILE" ]; then
    printf 'Exceptions file: %s\n' "$EXCEPTIONS_FILE"
fi
printf 'Scanning %s...\n\n' "$SCAN_MODE"

# shellcheck disable=SC2039
for FILE in $FILES; do
    TOTAL_FILES=$((TOTAL_FILES + 1))
    LINE_COUNT=$(wc -l < "$FILE")
    LINE_COUNT=$(printf '%s' "$LINE_COUNT" | tr -d '[:space:]')

    if ! is_exception "$FILE" "line-limit" && [ "$LINE_COUNT" -gt "$LIMIT" ]; then
        EXCEEDED_COUNT=$((EXCEEDED_COUNT + 1))
        printf '%s lines | %s\n' "$LINE_COUNT" "$FILE" >> "$EXCEEDED_LIST"
    fi

    if is_exception "$FILE" "header"; then
        continue
    fi

    if ! awk -v HeaderTemplate="$HEADER_TEMPLATE" '
        BEGIN {
            TemplateLineCount = 0;
            TemplateHeaderStarted = 0;
            TemplateReadLine = 0;
            while ((getline TemplateLine < HeaderTemplate) > 0) {
                TemplateReadLine++;
                if (TemplateReadLine == 1) {
                    sub(/^\357\273\277/, "", TemplateLine);
                }
                if (!TemplateHeaderStarted && TemplateLine ~ /^[[:space:]]*$/) {
                    continue;
                }
                TemplateHeaderStarted = 1;
                TemplateLineCount++;
                TemplateLines[TemplateLineCount] = TemplateLine;
                if (index(TemplateLine, "<Domain name>") > 0 ||
                    index(TemplateLine, "<Module name>") > 0) {
                    PlaceholderLines[TemplateLineCount] = 1;
                }
            }
            close(HeaderTemplate);
        }
        {
            FileLine = $0;
            if (NR == 1) {
                sub(/^\357\273\277/, "", FileLine);
            }
            if (!FileHeaderStarted && FileLine ~ /^[[:space:]]*$/) {
                next;
            }
            FileHeaderStarted = 1;
            FileHeaderLineCount++;
            if (FileHeaderLineCount <= TemplateLineCount) {
                FileLines[FileHeaderLineCount] = FileLine;
            }
        }
        END {
            if (FileHeaderLineCount < TemplateLineCount) {
                exit 1;
            }

            for (Index = 1; Index <= TemplateLineCount; Index++) {
                if (PlaceholderLines[Index]) {
                    Line = FileLines[Index];
                    gsub(/^[[:space:]]+|[[:space:]]+$/, "", Line);
                    if (Line == "") {
                        exit 1;
                    }
                    continue;
                }

                if (FileLines[Index] != TemplateLines[Index]) {
                    exit 1;
                }
            }
            exit 0;
        }
    ' "$FILE"; then
        HEADER_MISSING_COUNT=$((HEADER_MISSING_COUNT + 1))
        printf 'Missing header | %s\n' "$FILE" >> "$HEADER_MISSING_LIST"
    fi
done

if [ "$EXCEEDED_COUNT" -gt 0 ]; then
    printf 'Files above %s lines:\n' "$LIMIT"
    cat "$EXCEEDED_LIST"
    printf '\n'
fi

if [ "$HEADER_MISSING_COUNT" -gt 0 ]; then
    printf 'Files with invalid/missing header:\n'
    cat "$HEADER_MISSING_LIST"
    printf '\n'
fi

printf '\nScanned files: %s\n' "$TOTAL_FILES"
printf 'Files above %s lines: %s\n' "$LIMIT" "$EXCEEDED_COUNT"
printf 'Files with invalid/missing header: %s\n' "$HEADER_MISSING_COUNT"

if [ "$EXCEEDED_COUNT" -gt 0 ] || [ "$HEADER_MISSING_COUNT" -gt 0 ]; then
    exit 1
fi

exit 0
