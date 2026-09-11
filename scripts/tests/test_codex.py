"""Codex policy boundaries, protocol integration, and init backfills."""
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest

import agent_tools
import codex_hook
import init
from util import agent_setup, watchlist
import watch_pr

ENGINE = Path(__file__).resolve().parents[2]


def hook(payload, tmp_path, **env):
    environment = {**os.environ, 'WS_ASK': '', **env}
    return subprocess.run([sys.executable, str(ENGINE / 'scripts/codex_hook.py')],
        input=json.dumps({'cwd': str(tmp_path), **payload}), text=True, capture_output=True, env=environment)


@pytest.mark.parametrize('command', ['grep -rn foo docs', 'rg foo libs', '/usr/bin/grep foo a',
                                      'scripts/bgrep foo docs && grep foo libs', 'git grep foo',
                                      'env -u FOO grep needle docs', 'command env -i grep needle docs',
                                      'sudo -u nobody grep needle docs', '/usr/bin/env --unset=FOO rg needle docs'])
def test_search_is_explicit(tmp_path, command):
    result = hook({'hook_event_name': 'PreToolUse', 'tool_name': 'Bash',
                   'tool_input': {'command': command}}, tmp_path)
    assert result.returncode == 2
    assert 'bgrep' in result.stderr


@pytest.mark.parametrize('command', ['scripts/bgrep -rn Foo libs', 'rg --files docs',
                                      'git status --short', "printf '%s' 'grep foo'",
                                      'env -u FOO scripts/bgrep needle docs', 'command -v grep',
                                      'env -i rg --files docs'])
def test_non_search_and_bgrep_pass(tmp_path, command):
    assert hook({'hook_event_name': 'PreToolUse', 'tool_name': 'Bash',
                 'tool_input': {'command': command}}, tmp_path).returncode == 0


@pytest.mark.parametrize('tool', ['Bash', 'apply_patch', 'mcp__other__write_file', 'exec_command'])
def test_ask_refuses_shell_patch_and_other_mcp(tmp_path, tool):
    assert hook({'hook_event_name': 'PreToolUse', 'tool_name': tool,
                 'tool_input': {'command': 'touch source.cpp'}}, tmp_path, WS_ASK='1').returncode == 2


def test_ask_tools_and_session_capture(tmp_path):
    assert hook({'hook_event_name': 'PreToolUse', 'tool_name': 'mcp__bernini__read_file'},
                tmp_path, WS_ASK='1').returncode == 0
    sidfile = tmp_path / 'sessions/topic'
    result = hook({'hook_event_name': 'SessionStart', 'session_id': 'actual-thread'}, tmp_path,
                  WS_CODEX_SESSION_FILE=str(sidfile))
    assert result.returncode == 0, result.stderr
    assert sidfile.read_text().strip() == 'actual-thread'


def test_patch_triggers_shared_draft_scan(monkeypatch, tmp_path):
    calls = []
    monkeypatch.setattr(codex_hook, 'run', lambda name, payload: calls.append((name, payload)) or 0)
    monkeypatch.delenv('WS_ASK', raising=False)
    assert codex_hook.main({'hook_event_name': 'PostToolUse', 'tool_name': 'apply_patch', 'cwd': str(tmp_path)}) == 0
    assert calls[0][0] == 'draft_commit'
    assert calls[0][1]['tool_name'] == 'Bash'


def test_init_repairs_link_without_config_overwrite(monkeypatch, tmp_path):
    (tmp_path / 'CLAUDE.md').write_text('instructions')
    monkeypatch.setattr(init.ct, 'REPO_ROOT', str(tmp_path))
    monkeypatch.setattr(sys, 'argv', ['init.py', '--agents-only'])
    monkeypatch.setattr(init.cfg, 'load', lambda: pytest.fail('must not read/change config'))
    assert init.main() == 0
    assert os.readlink(tmp_path / 'AGENTS.md') == 'CLAUDE.md'
    assert init.main() == 0
    (tmp_path / 'CLAUDE.md').write_text('updated')
    assert (tmp_path / 'AGENTS.md').read_text() == 'updated'


def test_init_show_and_user_file(monkeypatch, tmp_path):
    (tmp_path / 'CLAUDE.md').write_text('instructions')
    monkeypatch.setattr(init.ct, 'REPO_ROOT', str(tmp_path))
    monkeypatch.setattr(sys, 'argv', ['init.py', '--agents-only', '--show'])
    assert init.main() == 0
    assert not (tmp_path / 'AGENTS.md').exists()
    (tmp_path / 'AGENTS.md').write_text('user instructions')
    agent_setup.instructions(tmp_path)
    assert (tmp_path / 'AGENTS.md').read_text() == 'user instructions'


def test_spec_rejects_escape_and_symlink(monkeypatch, tmp_path):
    monkeypatch.setattr(agent_tools, 'ROOT', tmp_path)
    outside = tmp_path / 'source.cpp'
    specs = tmp_path / 'docs/specs'
    specs.mkdir(parents=True)
    (specs / 'escape.md').symlink_to(outside)
    for path in ['source.cpp', 'docs/specs/../../source.md', 'docs/specs/escape.md']:
        with pytest.raises(ValueError):
            agent_tools.write_spec({'path': path, 'content': 'bad'})
    assert not outside.exists()
    assert 'Wrote' in agent_tools.write_spec({'path': 'docs/specs/idea.md', 'content': '# Idea'})
    assert (specs / 'idea.md').read_text() == '# Idea'


def test_stdio_mcp_end_to_end(tmp_path):
    (tmp_path / 'hello.txt').write_text('alpha\nbeta\n')
    messages = [
        {'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2024-11-05'}},
        {'method': 'notifications/initialized'},
        {'id': 2, 'method': 'tools/list'},
        {'id': 3, 'method': 'tools/call', 'params': {'name': 'read_file', 'arguments': {'path': 'hello.txt'}}},
        {'id': 4, 'method': 'tools/call', 'params': {'name': 'bgrep', 'arguments': {'pattern': 'beta', 'paths': ['hello.txt']}}},
    ]
    result = subprocess.run([sys.executable, str(ENGINE / 'scripts/agent_tools.py')], cwd=tmp_path,
                            input='\n'.join(map(json.dumps, messages)) + '\n', capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    assert [r['id'] for r in replies] == [1, 2, 3, 4]
    assert 'lsp' in {t['name'] for t in replies[1]['result']['tools']}
    assert '2: beta' in replies[2]['result']['content'][0]['text']
    assert 'beta' in replies[3]['result']['content'][0]['text']


def test_codex_watch_identity_and_wake(monkeypatch, tmp_path):
    monkeypatch.delenv('CLAUDE_CODE_SESSION_ID', raising=False)
    monkeypatch.setenv('CODEX_THREAD_ID', 'test-thread')
    assert watchlist._session() == 'test-thread'
    monkeypatch.setattr(watchlist, 'PATH', str(tmp_path / 'watchlist.json'))
    calls = []
    def run(args, **kwargs):
        calls.append(args)
        return subprocess.CompletedProcess(args, 0, '', '')
    monkeypatch.setattr(watch_pr.subprocess, 'run', run)
    watch_pr.notify_codex({'pr': 42, 'event': 'review', 'comments': ['review']})
    assert calls[0][:4] == ['codex', 'queue', '--thread', 'test-thread']
    assert json.loads((tmp_path / 'bernini-pr-event-42.json').read_text())['event'] == 'review'


@pytest.mark.parametrize('language', ['cpp', 'slang'])
def test_real_language_servers(monkeypatch, tmp_path, language):
    """Slang requires hierarchicalDocumentSymbolSupport; missing it crashes v1.8."""
    import shutil
    if not shutil.which('clangd' if language == 'cpp' else 'slangd'):
        pytest.skip('language server not installed')
    monkeypatch.setattr(agent_tools, 'ROOT', tmp_path)
    monkeypatch.setattr(agent_tools, 'SERVERS', {})
    suffix = 'cpp' if language == 'cpp' else 'slang'
    path = tmp_path / ('example.' + suffix)
    path.write_text('float twice(float x) { return x * 2; }\n')
    if language == 'cpp':
        (tmp_path / 'compile_commands.json').write_text(json.dumps([{
            'directory': str(tmp_path), 'file': str(path),
            'arguments': ['clang++', '-std=c++20', '-c', str(path)]}]))
    try:
        symbols = agent_tools.lsp({'operation': 'documentSymbol', 'path': path.name})
        assert symbols and 'twice' in json.dumps(symbols)
        path.write_text('float thrice(float x) { return x * 3; }\n')
        symbols = agent_tools.lsp({'operation': 'documentSymbol', 'path': path.name})
        assert 'thrice' in json.dumps(symbols)
        assert 'twice' not in json.dumps(symbols)
        if language == 'slang':
            with pytest.raises(ValueError, match='does not support'):
                agent_tools.lsp({'operation': 'findReferences', 'path': path.name, 'line': 1, 'character': 8})
    finally:
        for server in agent_tools.SERVERS.values():
            server.close()


def test_watcher_failure_wakes_codex_without_losing_undelivered_event(monkeypatch, tmp_path):
    monkeypatch.setenv('CODEX_THREAD_ID', 'test-thread')
    monkeypatch.setattr(watchlist, 'PATH', str(tmp_path / 'watchlist.json'))
    monkeypatch.setattr(watch_pr, 'NOTIFY_CODEX', True)
    monkeypatch.setattr(watch_pr, 'NOTIFY_PR', 42)
    def failure():
        raise SystemExit('poll failed')
    monkeypatch.setattr(watch_pr, 'main', failure)
    calls = []
    def run(args, **kwargs):
        calls.append(args)
        return subprocess.CompletedProcess(args, 0, '', '')
    monkeypatch.setattr(watch_pr.subprocess, 'run', run)
    with pytest.raises(SystemExit):
        watch_pr.entrypoint()
    event_path = tmp_path / 'bernini-pr-event-42.json'
    assert json.loads(event_path.read_text())['event'] == 'watcher_error'
    assert len(calls) == 1
    def cannot_deliver(args, **kwargs):
        return subprocess.CompletedProcess(args, 1, '', 'session unavailable')
    monkeypatch.setattr(watch_pr.subprocess, 'run', cannot_deliver)
    def review():
        watch_pr.notify_codex({'pr': 42, 'event': 'review', 'comments': ['important']})
    monkeypatch.setattr(watch_pr, 'main', review)
    with pytest.raises(watch_pr.NotificationError):
        watch_pr.entrypoint()
    assert json.loads(event_path.read_text())['event'] == 'review'


def test_watcher_registers_with_workspace(monkeypatch, tmp_path):
    monkeypatch.setenv('WS_AGENT_REGISTRY', str(tmp_path))
    watch_pr.register_with_workspace()
    pid, command = (tmp_path / str(os.getpid())).read_text().strip().split('\t', 1)
    assert int(pid) == os.getpid()
    assert command
    monkeypatch.setenv('WS_AGENT_REGISTRY', str(tmp_path / 'gone'))
    with pytest.raises(RuntimeError, match='ended'):
        watch_pr.register_with_workspace()


@pytest.mark.parametrize('session', ['claude', 'codex', 'human'])
def test_commit_attribution_tracks_the_assistant(tmp_path, session):
    message = tmp_path / 'message'
    message.write_text('feat: support both assistants\n')
    env = {k: v for k, v in os.environ.items() if k not in ('CLAUDECODE', 'CODEX_THREAD_ID')}
    if session == 'claude':
        env['CLAUDECODE'] = '1'
    elif session == 'codex':
        env['CODEX_THREAD_ID'] = 'test-thread'
    command = ['sh', str(ENGINE / '.githooks/prepare-commit-msg'), str(message), 'message']
    subprocess.run(command, env=env, check=True)
    subprocess.run(command, env=env, check=True)
    assert message.read_text().count('Co-authored-by: morgana-coding-agent[bot]') == (session != 'human')
