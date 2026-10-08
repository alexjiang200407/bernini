"""The API catalog: which flags a header parse keeps, which symbols count as public, and what a line says.

The flag filter and the comment handling are pure and tested directly. The walk is tested through
libclang over a fixture library, because what it decides -- a private member, a `detail` namespace,
an overload, a forward declaration -- is only as right as the AST it reads. The fixture includes no
standard header, so the wheel's bundled libclang parses it wherever the suite runs.
"""

import json
import os

import pytest

import api


def write(root, rel_path, text):
    path = os.path.join(root, *rel_path.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write(text)
    return path


FIXTURE_HEADER = """#pragma once

namespace demo
{
	/**
	 * Rounds `value` up to a multiple of `step`. Unlike a mask this takes any step.
	 *
	 * @param step Not zero.
	 */
	int round_up(int value, int step) noexcept;
	int round_up(long value, long step) noexcept;

	// Frame-time statistics over a window.
	class Window
	{
	public:
		/**
		 * @param size The number of samples kept.
		 */
		explicit Window(int size);
		Window(const Window& other);

		/** The mean of the samples held. */
		[[nodiscard]] float Mean() const noexcept
		{
			return m_Sum / float(m_Count);
		}

		void Push(float sample) = delete;

	private:
		void Recount();
		float m_Sum = 0;
		int   m_Count = 0;
	};

	struct Options
	{
		// Whether back faces are drawn. On by default.
		bool doubleSided = true;
		static constexpr int c_Max = 8;

		template <typename T>
		Options(T tag);
		Options(const float&& scale);
	};

	int twice(auto value);

	class Forward;

	namespace detail
	{
		int secret();
	}

	inline constexpr int c_Limit = 4;
}
"""


def fixture_tree(tmp_path):
    root = str(tmp_path)
    include = os.path.join(root, 'libs', 'demo', 'include')
    header = write(include, 'demo/window.h', FIXTURE_HEADER)
    source = write(root, 'libs/demo/src/window.cpp', '#include <demo/window.h>\n')
    db = os.path.join(root, 'compile_commands.json')
    with open(db, 'w', encoding='utf-8') as fh:
        json.dump([{'directory': root, 'file': source,
                    'arguments': ['clang++', f'-I{include}', '-std=c++20', '-Werror', '-c', source]}], fh)
    return root, include, header, db


def cindex_or_skip():
    try:
        return api.load_cindex(api.find_libclang('clang++'))
    except api.CatalogError as err:
        pytest.skip(str(err))


class TestParseFlags:
    def test_a_gnu_command_keeps_what_a_declaration_needs_and_nothing_else(self):
        entry = {'arguments': ['/usr/bin/clang++', '-DFOO=1', '-I/a', '-isystem', '/b', '-iframework', '/q',
                               '-std=gnu++20', '-Wall', '-Werror', '-Xclang', '-include-pch', '-Xclang', '/x.pch',
                               '-o', 'out.o', '-c', 'src.cpp']}
        compiler, flags = api.parse_flags(entry)
        assert compiler == '/usr/bin/clang++'
        assert flags == ['-DFOO=1', '-I/a', '-isystem', '/b', '-iframework', '/q', '-std=gnu++20']

    def test_a_cl_command_keeps_cl_spelling_under_its_driver_mode(self):
        """Translating `/I` into `-I` by hand would be a second parser of cl's options; libclang has one."""
        entry = {'command': 'cl.exe /nologo /DWIN32 /I C:\\inc /external:I C:\\vcpkg /std:c++20 /Zc:preprocessor '
                            '/permissive- /FoC:\\obj\\a.obj /YuPCH.h /c C:\\src\\a.cpp'}
        _, flags = api.parse_flags(entry)
        assert flags[0] == '--driver-mode=cl'
        for kept in ('/DWIN32', '/std:c++20', '/Zc:preprocessor', '/permissive-'):
            assert kept in flags
        assert flags[flags.index('/I') + 1] == 'C:\\inc'
        assert not any(f.startswith(('/Fo', '/Yu', '/nologo')) for f in flags)

    def test_a_joined_value_is_not_mistaken_for_a_separate_one(self):
        _, flags = api.parse_flags({'arguments': ['clang++', '-Ifoo', 'src.cpp']})
        assert flags == ['-Ifoo']


class TestEntryFor:
    def test_a_source_of_the_library_itself_wins(self, tmp_path):
        lib = os.path.join(str(tmp_path), 'libs', 'demo')
        consumer = {'directory': '/', 'file': '/app/main.cpp', 'arguments': ['c++', f'-I{lib}/include', '-DX', '-DY']}
        own = {'directory': lib, 'file': 'src/a.cpp', 'arguments': ['c++', f'-I{lib}/include']}
        assert api.entry_for([consumer, own], os.path.join(lib, 'include')) is own

    def test_a_header_only_library_takes_the_consumer_that_sees_most(self, tmp_path):
        lib = os.path.join(str(tmp_path), 'libs', 'demo')
        narrow = {'directory': '/', 'file': '/libs/demo/tests/t.cpp', 'arguments': ['c++', f'-I{lib}/include']}
        wide = {'directory': '/', 'file': '/app/main.cpp', 'arguments': ['c++', f'-I{lib}/include', '-I/qt']}
        unrelated = {'directory': '/', 'file': '/other.cpp', 'arguments': ['c++', '-I/a', '-I/b', '-I/c']}
        assert api.entry_for([narrow, unrelated, wide], os.path.join(lib, 'include')) is wide

    def test_nothing_naming_the_library_is_none(self, tmp_path):
        assert api.entry_for([{'directory': '/', 'file': '/x.cpp', 'arguments': ['c++']}],
                             os.path.join(str(tmp_path), 'libs', 'demo', 'include')) is None


class TestComments:
    def test_javadoc_framing_is_removed_and_paragraphs_kept(self):
        raw = '/**\n * First line.\n * Second.\n *\n * Next paragraph.\n */'
        assert api.clean_comment(raw) == 'First line.\nSecond.\n\nNext paragraph.'

    def test_line_comments_are_unframed(self):
        assert api.clean_comment('// One.\n// Two.') == 'One.\nTwo.'

    def test_the_brief_is_the_first_sentence(self):
        assert api.first_sentence('Rounds up, e.g. 5 to 8. Then more.') == 'Rounds up, e.g. 5 to 8.'

    def test_a_block_command_is_not_a_summary(self):
        assert api.first_sentence('@param size The count.\n\nKeeps a window.') == 'Keeps a window.'
        assert api.first_sentence('@param size The count.') == ''

    def test_a_long_sentence_is_cut(self):
        brief = api.first_sentence('word ' * 100)
        assert len(brief) == api.BRIEF_LIMIT and brief.endswith('…')


class TestDeclarationText:
    def test_the_body_and_comments_are_dropped(self):
        lines = ['template <typename T> // why', 'T twice(T v) noexcept', '{', '  return v * 2;', '}']
        assert api.declaration_text(lines, (1, 1), (5, 2)) == 'template <typename T> T twice(T v) noexcept'

    def test_a_member_initializer_list_is_not_declaration(self):
        lines = ['Arg(const std::integral auto v) : m_Value(v) {}']
        assert api.declaration_text(lines, (1, 1), (1, 49)) == 'Arg(const std::integral auto v)'
        lines = ['class Derived : public Base {']
        assert api.declaration_text(lines, (1, 1), (1, 30)) == 'class Derived : public Base'

    def test_a_brace_inside_a_default_argument_does_not_end_it(self):
        lines = ['void f(Options o = Options{}, int n = 1);']
        assert api.declaration_text(lines, (1, 1), (1, 42)) == 'void f(Options o = Options{}, int n = 1)'


class TestExtraction:
    def symbols(self, tmp_path):
        cindex = cindex_or_skip()
        _, include, _, _ = fixture_tree(tmp_path)
        symbols, errors = api.extract_library(cindex, include, [f'-I{include}', '-std=c++20'])
        assert errors == []
        by_name = {}
        for symbol in symbols:
            by_name.setdefault(symbol['name'], symbol)
        return by_name, symbols

    def test_the_public_surface_is_listed(self, tmp_path):
        by_name, _ = self.symbols(tmp_path)
        assert {'demo::round_up', 'demo::Window', 'demo::Window::Window', 'demo::Window::Mean',
                'demo::c_Limit', 'demo::Options::doubleSided', 'demo::Options::c_Max'} <= set(by_name)
        assert by_name['demo::Options::doubleSided']['kind'] == 'field'
        assert by_name['demo::Options::Options']['kind'] == 'constructor'
        assert by_name['demo::twice']['declaration'] == 'int twice(auto value)'
        assert by_name['demo::Options::doubleSided']['brief'] == 'Whether back faces are drawn.'

    def test_what_is_not_callable_from_outside_is_not(self, tmp_path):
        by_name, _ = self.symbols(tmp_path)
        for hidden in ('demo::Window::Recount', 'demo::Window::m_Sum', 'demo::detail::secret',
                       'demo::Forward', 'demo::Window::Push'):
            assert hidden not in by_name

    def test_every_overload_is_its_own_line_and_copies_are_not(self, tmp_path):
        _, symbols = self.symbols(tmp_path)
        assert [s['declaration'] for s in symbols if s['name'] == 'demo::round_up'] == [
            'int round_up(int value, int step) noexcept', 'int round_up(long value, long step) noexcept']
        assert [s['declaration'] for s in symbols if s['name'] == 'demo::Window::Window'] == [
            'explicit Window(int size)']

    def test_the_doc_and_its_brief_come_from_the_comment(self, tmp_path):
        by_name, _ = self.symbols(tmp_path)
        assert by_name['demo::round_up']['brief'] == 'Rounds `value` up to a multiple of `step`.'
        assert by_name['demo::Window']['brief'] == 'Frame-time statistics over a window.'
        assert by_name['demo::Window::Mean']['declaration'] == '[[nodiscard]] float Mean() const noexcept'
        assert by_name['demo::Window::Mean']['parent'] == 'demo::Window'
        assert by_name['demo::Window::Window']['brief'] == ''


class TestGenerate:
    def test_the_catalog_is_written_and_says_where_each_symbol_is(self, tmp_path):
        cindex_or_skip()
        root, include, _, db = fixture_tree(tmp_path)
        out = os.path.join(root, 'build', 'api')
        summary, parsed = api.generate({'demo': include}, db, out, root, log=lambda *_: None)
        assert parsed == {'demo'}
        assert summary['demo']['errors'] == 0

        with open(os.path.join(out, 'demo.md'), encoding='utf-8') as fh:
            text = fh.read()
        assert ('- `demo::round_up` (function) `int round_up(int value, int step) noexcept` — Rounds `value` '
                'up to a multiple of `step`. — libs/demo/include/demo/window.h:10') in text
        assert '  - `demo::Window::Mean` (method)' in text
        with open(os.path.join(out, 'INDEX.md'), encoding='utf-8') as fh:
            assert '[demo.md](demo.md)' in fh.read()
        with open(os.path.join(out, 'html', 'index.html'), encoding='utf-8') as fh:
            page = fh.read()
        assert '"demo::Window::Mean"' in page and '__DATA__' not in page

    def test_a_refresh_regenerates_only_when_a_header_changed(self, tmp_path):
        cindex_or_skip()
        root, include, header, db = fixture_tree(tmp_path)
        out = os.path.join(root, 'build', 'api')
        quiet = {'log': lambda *_: None}
        assert api.refresh({'demo': include}, db, out, root, **quiet) is not None
        assert api.refresh({'demo': include}, db, out, root, **quiet) is None

        with open(header, 'a', encoding='utf-8') as fh:
            fh.write('namespace demo { int added(); }\n')
        assert api.refresh({'demo': include}, db, out, root, **quiet) is not None
        with open(os.path.join(out, 'demo.md'), encoding='utf-8') as fh:
            assert '`demo::added`' in fh.read()

    def test_a_missing_compile_database_says_how_to_get_one(self, tmp_path):
        with pytest.raises(api.CatalogError, match='just build'):
            api.generate({'demo': str(tmp_path)}, os.path.join(str(tmp_path), 'nope.json'), str(tmp_path))

    def test_only_the_library_whose_headers_changed_is_parsed_again(self, tmp_path, monkeypatch):
        """A build refreshes the catalog after every header edit, so it pays for one library, not all."""
        cindex_or_skip()
        root, include, header, db = fixture_tree(tmp_path)
        other = os.path.join(root, 'libs', 'other', 'include')
        write(other, 'other/thing.h', 'namespace other { /** Does a thing. */ int thing(); }\n')
        with open(db, encoding='utf-8') as fh:
            entries = json.load(fh)
        entries[0]['arguments'].insert(1, f'-I{other}')
        with open(db, 'w', encoding='utf-8') as fh:
            json.dump(entries, fh)
        libraries = {'demo': include, 'other': other}
        out = os.path.join(root, 'build', 'api')
        api.refresh(libraries, db, out, root, log=lambda *_: None)

        parsed = []
        real = api.extract_library
        monkeypatch.setattr(api, 'extract_library', lambda c, inc, f: parsed.append(inc) or real(c, inc, f))
        with open(header, 'a', encoding='utf-8') as fh:
            fh.write('namespace demo { int added(); }\n')
        summary = api.refresh(libraries, db, out, root, log=lambda *_: None)

        assert parsed == [include]
        assert set(summary) == {'demo', 'other'}
        with open(os.path.join(out, 'other.md'), encoding='utf-8') as fh:
            assert '`other::thing`' in fh.read()

    def test_a_library_that_could_not_be_parsed_is_tried_again(self, tmp_path):
        """Stamped current while its old symbols stood in, it would stay stale until the next edit."""
        cindex_or_skip()
        root, include, header, db = fixture_tree(tmp_path)
        out = os.path.join(root, 'build', 'api')
        quiet = {'log': lambda *_: None}
        api.refresh({'demo': include}, db, out, root, **quiet)

        with open(db, encoding='utf-8') as fh:
            entries = json.load(fh)
        with open(db, 'w', encoding='utf-8') as fh:
            json.dump([{'directory': root, 'file': '/elsewhere.cpp', 'arguments': ['c++', '-I/nowhere']}], fh)
        with open(header, 'a', encoding='utf-8') as fh:
            fh.write('namespace demo { int added(); }\n')
        api.refresh({'demo': include}, db, out, root, **quiet)

        with open(db, 'w', encoding='utf-8') as fh:
            json.dump(entries, fh)
        assert api.refresh({'demo': include}, db, out, root, **quiet) is not None
        with open(os.path.join(out, 'demo.md'), encoding='utf-8') as fh:
            assert '`demo::added`' in fh.read()

    def test_a_parse_with_errors_is_tried_again(self, tmp_path):
        """A configure-only tree lacks the generated headers; the first build must parse again."""
        cindex_or_skip()
        root, include, header, db = fixture_tree(tmp_path)
        generated = os.path.join(root, 'build', 'generated')
        with open(header, 'a', encoding='utf-8') as fh:
            fh.write('#include "not_yet.h"\n')
        with open(db, encoding='utf-8') as fh:
            entries = json.load(fh)
        entries[0]['arguments'].insert(1, f'-I{generated}')
        with open(db, 'w', encoding='utf-8') as fh:
            json.dump(entries, fh)
        out = os.path.join(root, 'build', 'api')
        quiet = {'log': lambda *_: None}
        assert api.refresh({'demo': include}, db, out, root, **quiet)['demo']['errors'] == 1
        write(generated, 'not_yet.h', 'namespace demo { int generated(); }\n')
        assert api.refresh({'demo': include}, db, out, root, **quiet)['demo']['errors'] == 0
        assert api.refresh({'demo': include}, db, out, root, **quiet) is None

    def test_nothing_the_catalog_did_not_write_is_deleted(self, tmp_path):
        """`--out` is the caller's; a directory of hand-written Markdown must survive it."""
        cindex_or_skip()
        root, include, _, db = fixture_tree(tmp_path)
        out = os.path.join(root, 'docs')
        mine = write(out, 'guide.md', 'hand-written')
        api.refresh({'demo': include}, db, out, root, log=lambda *_: None)
        api.refresh({'demo': include}, db, out, root, force=True, log=lambda *_: None)
        assert os.path.isfile(mine)

    def test_a_removed_library_loses_its_file(self, tmp_path):
        cindex_or_skip()
        root, include, _, db = fixture_tree(tmp_path)
        out = os.path.join(root, 'build', 'api')
        api.refresh({'demo': include, 'twin': include}, db, out, root, log=lambda *_: None)
        assert os.path.isfile(os.path.join(out, 'twin.md'))
        assert api.refresh({'demo': include}, db, out, root, log=lambda *_: None) is not None
        assert not os.path.exists(os.path.join(out, 'twin.md'))

    def test_a_different_libclang_parses_everything_again(self, tmp_path, monkeypatch):
        """A catalog the fallback library parsed, errors and all, must not outlive the right one."""
        cindex_or_skip()
        root, include, _, db = fixture_tree(tmp_path)
        out = os.path.join(root, 'build', 'api')
        api.refresh({'demo': include}, db, out, root, log=lambda *_: None)
        real = api.find_libclang('clang++')
        monkeypatch.setattr(api, 'find_libclang', lambda compiler: None if real else '/other/libclang')
        monkeypatch.setattr(api, 'load_cindex', lambda library_file: pytest.importorskip('clang.cindex'))
        assert api.refresh({'demo': include}, db, out, root, log=lambda *_: None) is not None


class TestMain:
    def test_a_relative_root_resolves_against_the_caller(self, tmp_path, monkeypatch):
        """Each header is #included by path from a unit inside the include dir, not from the cwd."""
        cindex_or_skip()
        root, include, _, db = fixture_tree(tmp_path)
        monkeypatch.chdir(os.path.dirname(root))
        name = os.path.basename(root)
        assert api.main(['--root', name, '--library', 'demo=libs/demo/include', '--compile-db', db,
                         '--out', os.path.join(name, 'out'), '--quiet']) == 0
        with open(os.path.join(root, 'out', 'demo.md'), encoding='utf-8') as fh:
            assert '`demo::round_up`' in fh.read()


class TestFindLibclang:
    """The override is config.json's `libclang`; the environment is never read."""

    def use_config(self, monkeypatch, doc):
        import util.config as cfg
        monkeypatch.setattr(cfg, '_cache', doc)

    def test_the_configured_library_wins(self, tmp_path, monkeypatch):
        library = tmp_path / 'libclang.dll'
        library.write_bytes(b'')
        self.use_config(monkeypatch, {'libclang': str(library)})
        assert api.find_libclang('cl.exe') == str(library)

    def test_a_configured_library_that_is_not_there_is_an_error_naming_the_file(self, tmp_path, monkeypatch):
        self.use_config(monkeypatch, {'libclang': str(tmp_path / 'missing.dll')})
        with pytest.raises(api.CatalogError, match='config.json'):
            api.find_libclang('cl.exe')

    def test_the_old_environment_variable_is_ignored(self, tmp_path, monkeypatch):
        self.use_config(monkeypatch, {})
        monkeypatch.setenv('BERNINI_LIBCLANG', str(tmp_path / 'missing.dll'))
        assert api.find_libclang('cl.exe') != str(tmp_path / 'missing.dll')
