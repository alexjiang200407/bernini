#!/usr/bin/env python3
"""Bernini's stdio MCP tools: file reads, explicit bgrep, specs, clangd and slangd.

No dependencies beyond Python. MCP uses newline-delimited JSON; LSP uses
Content-Length frames. Language servers persist across calls and exit with us.
"""
import atexit
import fnmatch
import json
import os
import itertools
import tempfile
import time
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading

ENGINE = Path(os.environ.get('BERNINI_DIR') or Path(__file__).resolve().parent.parent).resolve()
ROOT = Path.cwd().resolve()
LIMIT = 40000


class LanguageServer:
    def __init__(self, language, root):
        executable = shutil.which('slangd' if language == 'slang' else 'clangd')
        if not executable and language == 'slang':
            executable = next(iter(ENGINE.glob('build/*/vcpkg_installed/*/tools/shader-slang/slangd')), None)
        if not executable:
            raise ValueError(f'{language} language server missing; install clangd or run ws init after building Slang')
        command = [str(executable)]
        if language != 'slang':
            command += ['--background-index', '--log=error']
            if not (root / 'compile_commands.json').exists():
                databases = sorted(root.glob('build/*/compile_commands.json'))
                if len(databases) == 1:
                    command += ['--compile-commands-dir=' + str(databases[0].parent)]
        self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=sys.stderr, cwd=root)
        self.messages = queue.Queue()
        self.seq = 0
        self.versions = {}
        self.language = language
        self.settings = {'slang': {'additionalSearchPaths': [
            str(ENGINE / 'libs/bgl_extended/shaders/src'),
            str(ENGINE / 'libs/bgl_common/shaders/src')]}} if language == 'slang' else {}
        threading.Thread(target=self._read, daemon=True).start()
        atexit.register(self.close)
        initialized = self.request('initialize', {'processId': os.getpid(), 'rootUri': root.as_uri(),
                     'capabilities': {'general': {'positionEncodings': ['utf-16']},
                                      'textDocument': {'synchronization': {'didSave': True},
                                          'documentSymbol': {'hierarchicalDocumentSymbolSupport': True}}},
                     'workspaceFolders': [{'uri': root.as_uri(), 'name': root.name}]})
        self.capabilities = initialized.get('capabilities', {})
        self.notify('initialized', {})
        if self.settings:
            self.notify('workspace/didChangeConfiguration', {'settings': self.settings})

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def _read(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.proc.stdout.readline()
                    if not line:
                        raise EOFError('language server exited')
                    if line in (b'\r\n', b'\n'):
                        break
                    k, v = line.decode().split(':', 1)
                    headers[k.lower()] = v.strip()
                size = int(headers['content-length'])
                data = self.proc.stdout.read(size)
                self.messages.put(json.loads(data))
        except Exception as e:
            self.messages.put(e)

    def send(self, payload):
        data = json.dumps({'jsonrpc': '2.0', **payload}).encode()
        self.proc.stdin.write(f'Content-Length: {len(data)}\r\n\r\n'.encode() + data)
        self.proc.stdin.flush()

    def notify(self, method, params):
        self.send({'method': method, 'params': params})

    def request(self, method, params):
        self.seq += 1
        ident = self.seq
        self.send({'id': ident, 'method': method, 'params': params})
        deadline = time.monotonic() + 45
        while True:
            try:
                msg = self.messages.get(timeout=max(.01, deadline - time.monotonic()))
            except queue.Empty as e:
                raise TimeoutError(f'{method} timed out') from e
            if isinstance(msg, Exception):
                raise msg
            if 'method' in msg and 'id' in msg:
                # Read-only client: never honor workspace/applyEdit.
                result = {'applied': False} if msg['method'] == 'workspace/applyEdit' else None
                if msg['method'] == 'workspace/configuration':
                    result = [self.configuration(item.get('section'))
                              for item in msg.get('params', {}).get('items', [])]
                self.send({'id': msg['id'], 'result': result})
            elif msg.get('id') == ident:
                if 'error' in msg:
                    raise ValueError(msg['error'])
                return msg.get('result')
            if time.monotonic() >= deadline:
                raise TimeoutError(f'{method} timed out')

    def configuration(self, section):
        value = self.settings
        for key in (section or '').split('.'):
            if not key:
                continue
            if not isinstance(value, dict):
                return None
            value = value.get(key)
        return value

    def open_file(self, path):
        text = path.read_text()
        uri = path.as_uri()
        old = self.versions.get(uri)
        if old is None:
            self.versions[uri] = (1, text)
            self.notify('textDocument/didOpen', {'textDocument': {
                'uri': uri, 'languageId': self.language, 'version': 1, 'text': text}})
        elif old[1] != text:
            self.versions[uri] = (old[0] + 1, text)
            lines = old[1].split('\n')
            end = {'line': len(lines) - 1, 'character': len(lines[-1].encode('utf-16-le')) // 2}
            self.notify('textDocument/didChange', {'textDocument': {
                'uri': uri, 'version': old[0] + 1}, 'contentChanges': [{
                    'range': {'start': {'line': 0, 'character': 0}, 'end': end}, 'text': text}]})
        return uri


SERVERS = {}
OPERATIONS = {
    'goToDefinition': 'textDocument/definition',
    'goToImplementation': 'textDocument/implementation',
    'findReferences': 'textDocument/references',
    'hover': 'textDocument/hover',
    'documentSymbol': 'textDocument/documentSymbol',
    'workspaceSymbol': 'workspace/symbol',
    'incomingCalls': 'callHierarchy/incomingCalls',
    'outgoingCalls': 'callHierarchy/outgoingCalls',
}


def lsp(args):
    operation = args['operation']
    if operation not in OPERATIONS:
        raise ValueError('unsupported LSP operation')
    path = (ROOT / args.get('path', '.')).resolve()
    language = args.get('language') or ('slang' if path.suffix == '.slang' else 'cpp')
    if language not in ('cpp', 'slang'):
        raise ValueError('language must be cpp or slang')
    server = SERVERS.get(language)
    if server is None or server.proc.poll() is not None:
        server = SERVERS[language] = LanguageServer(language, ROOT)
    capability = {
        'goToDefinition': 'definitionProvider', 'goToImplementation': 'implementationProvider',
        'findReferences': 'referencesProvider', 'hover': 'hoverProvider',
        'documentSymbol': 'documentSymbolProvider', 'workspaceSymbol': 'workspaceSymbolProvider',
        'incomingCalls': 'callHierarchyProvider', 'outgoingCalls': 'callHierarchyProvider',
    }[operation]
    if server.capabilities.get(capability) is None or server.capabilities.get(capability) is False:
        raise ValueError(f'{language} server does not support {operation}; use bgrep for an explicit text search')
    if operation == 'workspaceSymbol':
        return server.request(OPERATIONS[operation], {'query': args.get('query', '')})
    uri = server.open_file(path)
    params = {'textDocument': {'uri': uri}}
    if operation != 'documentSymbol':
        line, character = int(args.get('line', 1)), int(args.get('character', 1))
        if line < 1 or character < 1:
            raise ValueError('line and character are 1-based UTF-16 positions')
        params['position'] = {'line': line - 1, 'character': character - 1}
    if operation == 'findReferences':
        params['context'] = {'includeDeclaration': True}
    if operation in ('incomingCalls', 'outgoingCalls'):
        items = server.request('textDocument/prepareCallHierarchy', params) or []
        return [server.request(OPERATIONS[operation], {'item': item}) for item in items]
    return server.request(OPERATIONS[operation], params)


def read_file(args):
    path = (ROOT / args['path']).resolve()
    start = max(1, int(args.get('start', 1)))
    count = min(500, max(1, int(args.get('count', 200))))
    with path.open() as f:
        return ''.join(f'{i}: {line}' for i, line in enumerate(
            itertools.islice(f, start - 1, start - 1 + count), start))[:LIMIT]


def list_files(args):
    base = (ROOT / args.get('path', '.')).resolve()
    pattern = args.get('pattern', '*')
    found = []
    for directory, dirs, files in os.walk(base):
        dirs[:] = sorted(d for d in dirs if d not in ('.git', 'build', '.ws', 'vcpkg_installed'))
        for name in sorted(files):
            relative = str((Path(directory) / name).relative_to(base))
            if fnmatch.fnmatch(relative, pattern):
                found.append(relative)
                if len(found) >= 1000:
                    return '\n'.join(found) + '\n[truncated; narrow the path or pattern]'
    return '\n'.join(found)


def bgrep(args):
    paths = args.get('paths') or ['.']
    if not isinstance(paths, list) or not all(isinstance(p, str) for p in paths):
        raise ValueError('paths must be a list of paths')
    # Absolute operands and -e keep leading dashes literal. No shell or arbitrary flags.
    command = [str(ENGINE / 'scripts/bgrep'), '-rnI', '--exclude-dir=.git', '--exclude-dir=build']
    if args.get('literal'):
        command += ['-F']
    command += ['-e', args['pattern'], '--'] + [str((ROOT / p).resolve()) for p in paths]
    with tempfile.TemporaryFile() as output:
        done = subprocess.run(command, stdout=output, stderr=subprocess.PIPE, timeout=30)
        output.seek(0)
        text = output.read(LIMIT + 1).decode(errors='replace')
    if done.returncode not in (0, 1):
        raise ValueError(done.stderr.decode(errors='replace'))
    return text[:LIMIT] + ('\n[truncated; narrow the search]' if len(text) > LIMIT else '')


def write_spec(args):
    target = (ROOT / args['path']).resolve()
    specs = (ROOT / 'docs/specs').resolve()
    if target.suffix != '.md' or specs not in target.parents:
        raise ValueError('Only docs/specs/*.md may be written')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(args['content'])
    # Also works when invoked from a client without PostToolUse hooks.
    done = subprocess.run([sys.executable, str(ENGINE / '.claude/hooks/draft_commit.py')],
        input=json.dumps({'cwd': str(ROOT), 'tool_name': 'Write', 'tool_input': {'file_path': str(target)}}),
        text=True, capture_output=True, env={**os.environ, 'CLAUDE_PROJECT_DIR': str(ROOT)})
    if done.returncode:
        raise ValueError(f'Spec written but draft commit failed: {done.stderr}')
    return f'Wrote {target}'


def tool(name, description, properties, required=()):
    return {'name': name, 'description': description, 'inputSchema': {
        'type': 'object', 'properties': properties, 'required': list(required), 'additionalProperties': False}}


STRING = {'type': 'string'}
INTEGER = {'type': 'integer'}
TOOLS = [
    tool('read_file', 'Read a text file with line numbers; no shell.',
         {'path': STRING, 'start': INTEGER, 'count': INTEGER}, ['path']),
    tool('list_files', 'List files by glob; excludes build and git directories.',
         {'path': STRING, 'pattern': STRING}),
    tool('bgrep', 'Explicit text/completeness search through scripts/bgrep. Prefer LSP for symbols.',
         {'pattern': STRING, 'paths': {'type': 'array', 'items': STRING}, 'literal': {'type': 'boolean'}}, ['pattern']),
    tool('write_spec', 'Write a Markdown spec only under docs/specs and commit the shared draft.',
         {'path': STRING, 'content': STRING}, ['path', 'content']),
    tool('lsp', 'Navigate C++ with clangd or Slang with slangd. Positions are 1-based UTF-16. '
         'Requires a configured compile database for accurate C++ answers.',
         {'operation': {'type': 'string', 'enum': list(OPERATIONS)}, 'path': STRING,
          'line': INTEGER, 'character': INTEGER, 'query': STRING,
          'language': {'type': 'string', 'enum': ['cpp', 'slang']}}, ['operation']),
]
FUNCTIONS = {'read_file': read_file, 'list_files': list_files, 'bgrep': bgrep, 'write_spec': write_spec, 'lsp': lsp}


def dispatch(message):
    method = message['method']
    if method == 'initialize':
        return {'protocolVersion': message.get('params', {}).get('protocolVersion', '2024-11-05'),
                'capabilities': {'tools': {}}, 'serverInfo': {'name': 'bernini', 'version': '1.0.0'}}
    if method == 'ping':
        return {}
    if method == 'tools/list':
        return {'tools': TOOLS}
    if method == 'tools/call':
        params = message['params']
        try:
            result = FUNCTIONS[params['name']](params.get('arguments', {}))
            text = result if isinstance(result, str) else json.dumps(result, indent=2)
            return {'content': [{'type': 'text', 'text': text[:LIMIT]}]}
        except Exception as e:
            return {'isError': True, 'content': [{'type': 'text', 'text': str(e) or type(e).__name__}]}
    raise ValueError(f'Unknown MCP method: {method}')


def main():
    for line in sys.stdin:
        message = {}
        try:
            message = json.loads(line)
            if 'id' not in message:
                continue
            reply = {'jsonrpc': '2.0', 'id': message['id'], 'result': dispatch(message)}
        except Exception as e:
            reply = {'jsonrpc': '2.0', 'id': message.get('id'), 'error': {'code': -32603, 'message': str(e)}}
        print(json.dumps(reply), flush=True)


if __name__ == '__main__':
    main()
