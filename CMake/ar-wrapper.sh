#!/bin/bash
# Wrapper for ar that handles "Argument list too long" by batching objects.
# Usage: ar-wrapper.sh <ar-binary> <flags> <archive> [objects...]

AR="$1"
FLAGS="$2"
TARGET="$3"
shift 3

BATCH_SIZE=200
batch=()
first=1

for obj in "$@"; do
    batch+=("$obj")
    if [ ${#batch[@]} -ge $BATCH_SIZE ]; then
        if [ $first -eq 1 ]; then
            "$AR" $FLAGS "$TARGET" "${batch[@]}"
            first=0
        else
            "$AR" q "$TARGET" "${batch[@]}"
        fi
        batch=()
    fi
done

if [ ${#batch[@]} -gt 0 ]; then
    if [ $first -eq 1 ]; then
        "$AR" $FLAGS "$TARGET" "${batch[@]}"
    else
        "$AR" q "$TARGET" "${batch[@]}"
    fi
fi
