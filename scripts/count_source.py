#!/usr/bin/env python3
import os
from collections import defaultdict
import fnmatch

EXTENSION_MAP = {
    '.cpp': 'C++',
    '.h': 'C++',
    '.slang': 'Slang Shading Language',
    '.py': 'Python'
}

# Longest match wins, so `libs/assetlib/cli` beats the `libs/assetlib` it sits inside. Matched on
# path components rather than characters, which is what keeps `libs/assetlib` off
# `libs/assetlib_structs`.
MODULE_PREFIXES = (
    ('libs/bgl_extended', 'bgl_extended'),
    ('libs/bgl_intfc', 'bgl_intfc'),
    ('libs/core', 'core'),
    ('libs/assetlib', 'assetlib'),
    ('libs/assetlib/cli', 'assetlib_cli'),
    ('libs/assetlib_structs', 'assetlib_structs'),
    ('libs/gamelib', 'gamelib'),
    ('apps/editor', 'editor'),
    ('examples', 'examples'),
    ('scripts', 'scripts'),
    ('.claude/hooks', 'claude-hooks'),
    ('PCH', 'pch'),
)

UNCLASSIFIED = '(unclassified)'

def parse_gitignore(gitignore_path):
    """Simple parser to read .gitignore rules and turn them into matchable patterns."""
    patterns = []
    if os.path.exists(gitignore_path):
        with open(gitignore_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    if line.endswith('/'):
                        line = line[:-1]
                    patterns.append(line)
    return patterns

def is_ignored(path, base_dir, gitignore_patterns):
    """Check if a path matches gitignore, is a submodule, or resides in an 'extern' folder."""
    rel_path = os.path.relpath(path, base_dir)
    parts = rel_path.split(os.sep)
    
    # Always ignore .git directory (handles submodules)
    if '.git' in parts:
        return True
        
    # Explicitly exclude any 'extern' directory
    if 'extern' in parts:
        return True
        
    for pattern in gitignore_patterns:
        if fnmatch.fnmatch(rel_path, pattern) or any(fnmatch.fnmatch(part, pattern) for part in parts):
            return True
    return False

def is_test(path, base_dir):
    """Whether a file is test code: anything under a 'tests' directory, or a pytest file.

    Keyed on the directory rather than a *_test.cpp suffix so the harness files that sit beside the
    cases -- main.cpp, the golden-image helpers, the test pch -- count as tests too, instead of
    inflating the production tally.

    Python adds the file-name half, because pytest collects `test_*.py` and `conftest.py`
    wherever they sit: one written beside the script it covers is still a test, and counting it
    as production would inflate the tally the directory rule exists to protect.
    """
    rel_path = os.path.relpath(path, base_dir)
    if 'tests' in rel_path.split(os.sep):
        return True

    name = os.path.basename(path)
    return name == 'conftest.py' or (name.startswith('test_') and name.endswith('.py'))

def module_of(rel_path):
    """The module a repo-relative path belongs to, or UNCLASSIFIED when the table does not cover it.

    A hand-maintained table drifts as directories move, so an unmatched path is given a row of its
    own rather than folded into a neighbour, where the drift would be invisible.

    Both separators are accepted: `os.path.relpath` yields `\\` on Windows, the table is written
    with `/`.
    """
    parts = rel_path.replace('\\', '/').split('/')

    depth = 0
    module = UNCLASSIFIED
    for prefix, name in MODULE_PREFIXES:
        expected = prefix.split('/')
        if len(expected) > depth and parts[:len(expected)] == expected:
            depth, module = len(expected), name
    return module

def new_tally():
    return {'src': {'files': 0, 'lines': 0}, 'test': {'files': 0, 'lines': 0}}

def collect(base_dir):
    """Walk `base_dir` and tally every source file by language, by module, and in total.

    The two breakdowns partition the same files, so their totals are equal by construction -- an
    inequality means a file was dropped or counted twice.

    @param base_dir Directory to walk; its `.gitignore` is what decides the exclusions.
    @return `(by_language, by_module, totals)`, each a src/test tally of files and lines.
    """
    by_language = defaultdict(new_tally)
    by_module = defaultdict(new_tally)
    totals = new_tally()

    gitignore_patterns = parse_gitignore(os.path.join(base_dir, '.gitignore'))

    for root, dirs, files in os.walk(base_dir):
        # Prevent os.walk from entering ignored or 'extern' directories
        dirs[:] = [d for d in dirs if not is_ignored(os.path.join(root, d), base_dir, gitignore_patterns)]

        for file in files:
            file_path = os.path.join(root, file)

            if is_ignored(file_path, base_dir, gitignore_patterns):
                continue

            ext = os.path.splitext(file)[1].lower()
            if ext not in EXTENSION_MAP:
                continue

            language = EXTENSION_MAP[ext]
            module = module_of(os.path.relpath(file_path, base_dir))
            kind = 'test' if is_test(file_path, base_dir) else 'src'

            try:
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    line_count = sum(1 for _ in f)
            except Exception as e:
                print(f"Could not read file {file_path}: {e}")
                continue

            for tally in (by_language[language], by_module[module], totals):
                tally[kind]['files'] += 1
                tally[kind]['lines'] += line_count

    return by_language, by_module, totals

def print_table(heading, stats, totals):
    print(f"{'':<25} | {'Source':^21} | {'Tests':^21}")
    print(f"{heading:<25} | {'Files':<8} | {'Lines':<10} | {'Files':<8} | {'Lines':<10}")
    print(f"{'-'*25}-|-{'-'*8}-|-{'-'*10}-|-{'-'*8}-|-{'-'*10}")

    ranked = sorted(stats.items(), key=lambda x: x[1]['src']['lines'] + x[1]['test']['lines'], reverse=True)
    for name, data in ranked:
        print(f"{name:<25} | {data['src']['files']:<8} | {data['src']['lines']:<10,} | "
              f"{data['test']['files']:<8} | {data['test']['lines']:<10,}")

    print(f"{'-'*25}-|-{'-'*8}-|-{'-'*10}-|-{'-'*8}-|-{'-'*10}")
    print(f"{'TOTAL':<25} | {totals['src']['files']:<8} | {totals['src']['lines']:<10,} | "
          f"{totals['test']['files']:<8} | {totals['test']['lines']:<10,}")

def count_lines_and_files():
    by_language, by_module, totals = collect(os.getcwd())

    print(f"\n{'='*66}")
    print(f" SOURCE CODE METRICS")
    print(f"{'='*66}\n")

    print_table('Language', by_language, totals)
    print()
    print_table('Module', by_module, totals)

    all_files = totals['src']['files'] + totals['test']['files']
    all_lines = totals['src']['lines'] + totals['test']['lines']
    ratio = (totals['test']['lines'] / totals['src']['lines']) if totals['src']['lines'] else 0.0

    print(f"\n{'Combined':<25} | {all_files:<8} | {all_lines:<10,}")
    print(f"{'Test lines / source line':<25} | {ratio:.2f}\n")

if __name__ == "__main__":
    count_lines_and_files()
