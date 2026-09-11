#!/usr/bin/env python3
"""Require explicit bgrep for text search; reserve LSP for symbol navigation.

This is a workflow guard, not a shell sandbox. It catches direct search tools
and common shell invocations without attempting to interpret arbitrary code.
"""
import json
import os
import shlex
import sys

SEARCH = {'grep', 'egrep', 'fgrep', 'rg', 'ripgrep', 'select-string', 'sls', 'findstr'}
MESSAGE = 'Use LSP for symbol navigation. For text or completeness searches use scripts/bgrep (or the bernini bgrep MCP tool). rg --files is allowed for file discovery.'


WRAPPERS = {
    'command': ({'-p'}, set()),
    'exec': ({'-c', '-l'}, {'-a'}),
    'env': ({'-i', '--ignore-environment', '-0', '--null'}, {'-u', '--unset', '-C', '--chdir'}),
    'sudo': ({'-n', '-E', '-H', '-P', '-S', '-b', '-k', '-K'},
             {'-u', '--user', '-g', '--group', '-h', '--host', '-p', '--prompt', '-C', '-T', '-R', '-D'}),
}


def command_name(value):
    name = value.replace('\\', '/').rsplit('/', 1)[-1].lower()
    return name[:-4] if name.endswith('.exe') else name


def unwrap(args):
    while args:
        if '=' in args[0] and not args[0].startswith('-'):
            args = args[1:]
            continue
        name = command_name(args[0])
        if name not in WRAPPERS:
            return args
        args = args[1:]
        flags, operands = WRAPPERS[name]
        if name == 'command' and args and args[0] in ('-v', '-V'):
            return []  # Command lookup does not run the named program.
        while args and args[0].startswith('-'):
            option = args[0]
            args = args[1:]
            if option == '--':
                break
            if option in flags:
                continue
            if option in operands and args:
                args = args[1:]
                continue
            if option.partition('=')[0] in operands and '=' in option:
                continue
            if any(len(key) == 2 and option.startswith(key) and len(option) > 2 for key in operands):
                continue
            return None  # An unresolved wrapper must not hide its search command.
    return args


def refused(command, windows=False):
    lexer = shlex.shlex(command, posix=True, punctuation_chars=';&|()')
    if windows:
        lexer.escape = ""
    lexer.whitespace_split = True
    tokens = list(lexer)
    commands, current = [], []
    for token in tokens:
        if token and all(c in ';&|()' for c in token):
            commands.append(current)
            current = []
        else:
            current.append(token)
    commands.append(current)
    for args in commands:
        args = unwrap(args)
        if args is None:
            return True
        if not args:
            continue
        name = command_name(args[0])
        if name in ('bash', 'sh', 'zsh') and '-c' in args:
            index = args.index('-c') + 1
            if index < len(args) and refused(args[index], windows=windows):
                return True
        if name == 'git' and len(args) > 1 and args[1] == 'grep':
            return True
        if name in SEARCH and not (name in ('rg', 'ripgrep') and '--files' in args[1:]):
            return True
    return False


def main(payload):
    tool = payload.get('tool_name', '')
    if tool == 'Grep' or (tool in ('Bash', 'PowerShell') and refused(payload.get('tool_input', {}).get('command', ''), windows=tool == 'PowerShell' or sys.platform == 'win32')):
        print(MESSAGE, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(json.load(sys.stdin)))
    except (ValueError, TypeError, AttributeError) as e:
        print(f'Cannot check search command: {e}', file=sys.stderr)
        sys.exit(2)
