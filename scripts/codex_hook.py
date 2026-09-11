#!/usr/bin/env python3
"""Adapt Codex lifecycle payloads to Bernini's shared Claude hook policies."""
import json
import os
from pathlib import Path
import subprocess
import sys

ENGINE = Path(__file__).resolve().parent.parent
HOOKS = ENGINE / '.claude' / 'hooks'


def run(name, payload):
    local_hook = Path(payload.get('cwd') or os.getcwd()) / '.claude/hooks' / (name + '.py')
    script = local_hook if local_hook.is_file() else HOOKS / (name + '.py')
    done = subprocess.run([sys.executable, str(script)],
                          input=json.dumps(payload), text=True, capture_output=True)
    if done.stdout:
        print(done.stdout, end='')
    if done.stderr:
        print(done.stderr, end='', file=sys.stderr)
    # A crashed policy must fail closed, too.
    return 2 if done.returncode else 0


def main(payload):
    event = payload.get('hook_event_name')
    root = Path(payload.get('cwd') or os.getcwd()).resolve()
    os.environ['CLAUDE_PROJECT_DIR'] = str(root)
    if payload.get('session_id'):
        os.environ['CODEX_THREAD_ID'] = payload['session_id']
    if event == 'SessionStart':
        sidfile = os.environ.get('WS_CODEX_SESSION_FILE')
        if sidfile and payload.get('session_id') and not payload.get('agent_id'):
            target = Path(sidfile)
            target.parent.mkdir(parents=True, exist_ok=True)
            tmp = target.with_suffix('.tmp')
            tmp.write_text(payload['session_id'] + '\n')
            tmp.replace(target)
        return 0
    tool = payload.get('tool_name', '')
    if event == 'PreToolUse':
        if os.environ.get('WS_ASK'):
            # Codex has no native Read/Grep/Glob. Our MCP server provides those
            # operations without a shell, and enforces spec-only writes itself.
            if not tool.startswith('mcp__bernini__'):
                print('ws ask has no shell or arbitrary editing tools. Use the bernini MCP '
                      'read_file, list_files, bgrep, lsp and write_spec tools.', file=sys.stderr)
                return 2
        if tool == 'Bash':
            for hook in ('gh_guard', 'lsp_nudge', 'search_guard'):
                if run(hook, payload):
                    return 2
        if tool in ('Grep', 'grep', 'search_files'):
            print('Use LSP for symbols or bernini bgrep for explicit text searches.', file=sys.stderr)
            return 2
    elif event == 'PostToolUse':
        if tool == 'Bash' or tool == 'apply_patch' or tool.startswith('mcp__bernini__'):
            # Scan the shared draft worktree after any write, including patches
            # that touch several files or rename one (not a single file_path).
            return run('draft_commit', {**payload, 'tool_name': 'Bash'})
    elif event == 'Stop':
        return run('pr_watch_guard', payload)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(json.load(sys.stdin)))
    except Exception as e:
        print(f'Bernini hook failed: {e}', file=sys.stderr)
        sys.exit(2)
