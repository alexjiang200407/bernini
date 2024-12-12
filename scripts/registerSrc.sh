#!/usr/bin/env bash

out="./cmake/sourcelist.cmake"

echo "set(SOURCES">"$out"

find "src" -type f >> "$out"

find "include" -type f >> "$out"


echo ")">>"$out"