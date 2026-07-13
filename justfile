# Bernini task runner. `just` on its own lists the recipes.
#
# This is a convenience layer, not the contract: every recipe is a one-line call
# into scripts/, and `python scripts/<script>.py ...` stays the canonical
# invocation for CI, for agents, and for a clone that hasn't installed just.
#
# Install just with:  pip install -r scripts/requirements.txt

# Run recipes through cmd on Windows rather than just's default `sh`. Git Bash
# provides sh, but its usr/bin is usually absent from the system PATH, so a
# default-shell recipe would work from bash and fail from PowerShell. Recipes here
# are single commands, so cmd costs us nothing.
set windows-shell := ["cmd.exe", "/c"]

python := if os_family() == "windows" { "python" } else { "python3" }

[private]
default:
    @just --list --unsorted

# Generate scripts/config.json for this machine (preset, toolchain, precommand).
init *args:
    @{{ python }} scripts/init.py {{ args }}

# Configure and build a target (default: all).
build *args:
    @{{ python }} scripts/build.py {{ args }}

# Build an executable target and run it, with cwd set to its output directory.
run target *args:
    @{{ python }} scripts/exec_target.py {{ target }} {{ args }}

# Build and run every test suite (or only those matching the given names).
test *args:
    @{{ python }} scripts/run_tests.py {{ args }}

# Build the CLI tools and stage them (with their DLLs) into ./dist, for PATH.
install *args:
    @{{ python }} scripts/install.py {{ args }}

# clang-format files in place (--check to verify only).
format *args:
    @{{ python }} scripts/format.py {{ args }}

# Regenerate the IDL C++ headers and Slang copies.
idl *args:
    @{{ python }} scripts/gen_idl.py {{ args }}

# List every CMake target.
targets *args:
    @{{ python }} scripts/get_targets.py {{ args }}

# Resolve the paths of built executables.
exes *args:
    @{{ python }} scripts/find_executables.py {{ args }}

# Count source files and lines by language.
count:
    @{{ python }} scripts/count_source.py
