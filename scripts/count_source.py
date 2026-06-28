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

def count_lines_and_files():
    extension_map = {
        '.cpp': 'C++',
        '.h': 'C++',
        '.slang': 'Slang Shading Language',
        '.py': 'Python'
    }
    
    stats = defaultdict(lambda: {'files': 0, 'lines': 0})
    total_files = 0
    total_lines = 0
    
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
                
                try:
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        line_count = sum(1 for _ in f)
                        
                    stats[language]['files'] += 1
                    stats[language]['lines'] += line_count
                    total_files += 1
                    total_lines += line_count
                except Exception as e:
                    print(f"Could not read file {file_path}: {e}")

    # Print the breakdown results
    print(f"\n{'='*50}")
    print(f" SOURCE CODE METRICS")
    print(f"{'='*50}\n")
    
    print(f"{'Language':<25} | {'Files':<8} | {'Lines of Code':<12}")
    print(f"{'-'*25}-|-{'-'*8}-|-{'-'*12}")
    
    for lang, data in sorted(stats.items(), key=lambda x: x[1]['lines'], reverse=True):
        print(f"{lang:<25} | {data['files']:<8} | {data['lines']:<12,}")
        
    print(f"{'-'*50}")
    print(f"{'TOTAL':<25} | {total_files:<8} | {total_lines:<12,}\n")

if __name__ == "__main__":
    count_lines_and_files()