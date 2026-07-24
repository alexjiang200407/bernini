#!/usr/bin/env python3
import os
from collections import defaultdict
import fnmatch

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
    """Whether a file is test code: anything under a 'tests' directory.

    Keyed on the directory rather than a *_test.cpp suffix so the harness files that sit beside the
    cases -- main.cpp, the golden-image helpers, the test pch -- count as tests too, instead of
    inflating the production tally.
    """
    rel_path = os.path.relpath(path, base_dir)
    return 'tests' in rel_path.split(os.sep)

def count_lines_and_files():
    extension_map = {
        '.cpp': 'C++',
        '.h': 'C++',
        '.slang': 'Slang Shading Language',
        '.py': 'Python'
    }

    stats = defaultdict(lambda: {'src': {'files': 0, 'lines': 0}, 'test': {'files': 0, 'lines': 0}})
    totals = {'src': {'files': 0, 'lines': 0}, 'test': {'files': 0, 'lines': 0}}

    cwd = os.getcwd()
    gitignore_patterns = parse_gitignore(os.path.join(cwd, '.gitignore'))

    for root, dirs, files in os.walk(cwd):
        # Prevent os.walk from entering ignored or 'extern' directories
        dirs[:] = [d for d in dirs if not is_ignored(os.path.join(root, d), cwd, gitignore_patterns)]

        for file in files:
            file_path = os.path.join(root, file)

            if is_ignored(file_path, cwd, gitignore_patterns):
                continue

            ext = os.path.splitext(file)[1].lower()
            if ext in extension_map:
                language = extension_map[ext]
                kind = 'test' if is_test(file_path, cwd) else 'src'

                try:
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        line_count = sum(1 for _ in f)

                    stats[language][kind]['files'] += 1
                    stats[language][kind]['lines'] += line_count
                    totals[kind]['files'] += 1
                    totals[kind]['lines'] += line_count
                except Exception as e:
                    print(f"Could not read file {file_path}: {e}")

    # Print the breakdown results
    print(f"\n{'='*66}")
    print(f" SOURCE CODE METRICS")
    print(f"{'='*66}\n")

    print(f"{'':<25} | {'Source':^21} | {'Tests':^21}")
    print(f"{'Language':<25} | {'Files':<8} | {'Lines':<10} | {'Files':<8} | {'Lines':<10}")
    print(f"{'-'*25}-|-{'-'*8}-|-{'-'*10}-|-{'-'*8}-|-{'-'*10}")

    ranked = sorted(stats.items(), key=lambda x: x[1]['src']['lines'] + x[1]['test']['lines'], reverse=True)
    for lang, data in ranked:
        print(f"{lang:<25} | {data['src']['files']:<8} | {data['src']['lines']:<10,} | "
              f"{data['test']['files']:<8} | {data['test']['lines']:<10,}")

    print(f"{'-'*25}-|-{'-'*8}-|-{'-'*10}-|-{'-'*8}-|-{'-'*10}")
    print(f"{'TOTAL':<25} | {totals['src']['files']:<8} | {totals['src']['lines']:<10,} | "
          f"{totals['test']['files']:<8} | {totals['test']['lines']:<10,}")

    all_files = totals['src']['files'] + totals['test']['files']
    all_lines = totals['src']['lines'] + totals['test']['lines']
    ratio = (totals['test']['lines'] / totals['src']['lines']) if totals['src']['lines'] else 0.0

    print(f"\n{'Combined':<25} | {all_files:<8} | {all_lines:<10,}")
    print(f"{'Test lines / source line':<25} | {ratio:.2f}\n")

if __name__ == "__main__":
    count_lines_and_files()