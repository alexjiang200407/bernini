#!/bin/sh
if [ "$#" -ne 1 ]; then
  echo "Usage: $0 NAME" >&2
  exit 1
fi

PROJECT_NAME="$1"

# Create cmake directory if it doesn't exist
mkdir -p cmake

# Generate headerlist.cmake
echo "set(${PROJECT_NAME}_HEADERS" > cmake/headerlist.cmake
if [ -d "include" ]; then
  find include -type f \( -name '*.h' -o -name '*.hpp' \) -printf '\t%p\n' >> cmake/headerlist.cmake
elif [ -d "src" ]; then
  find src -type f \( -name '*.h' -o -name '*.hpp' \) -printf '\t%p\n' >> cmake/headerlist.cmake
fi
echo ")" >> cmake/headerlist.cmake

# Generate sourcelist.cmake
echo "set(${PROJECT_NAME}_SOURCES" > cmake/sourcelist.cmake
if [ -d "src" ]; then
  find src -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.cc' \) -printf '\t%p\n' >> cmake/sourcelist.cmake
fi
echo ")" >> cmake/sourcelist.cmake

echo "Generated cmake/headerlist.cmake and cmake/sourcelist.cmake for project: ${PROJECT_NAME}"