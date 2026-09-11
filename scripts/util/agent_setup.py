"""Idempotent, non-destructive setup of the instructions shared by both agents."""
from pathlib import Path
import os
import sys


def instructions(root):
    root = Path(root)
    source, target = root / 'CLAUDE.md', root / 'AGENTS.md'
    if not source.is_file():
        return
    if target.is_symlink():
        if os.readlink(target) == 'CLAUDE.md':
            return
        target.unlink()
    elif target.exists():
        print(f'warning: keeping existing {target}; expected a symlink to CLAUDE.md', file=sys.stderr)
        return
    try:
        target.symlink_to('CLAUDE.md')
    except OSError as e:
        raise RuntimeError(f'Cannot create {target}: {e}. On Windows enable Developer Mode for symlinks.') from e
