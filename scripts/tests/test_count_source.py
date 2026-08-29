"""The module breakdown `just count` prints: which module a path lands in, and that none is lost.

Two things can silently go wrong here. A path can be attributed to the wrong module -- the table is
hand-maintained and `libs/assetlib` sits directly above `libs/assetlib/cli` -- and a file can be
dropped or double-counted between the two tables, which would make the report quietly untrue while
still printing. The first is pinned by naming the pairs that trap a shorter rule; the second by
walking a fixture tree and asserting the two breakdowns partition it identically.
"""

import os

import count_source


def write(root, rel_path, lines):
    """Create `rel_path` under `root` with `lines` lines, making its directories."""
    path = os.path.join(root, *rel_path.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join('x' for _ in range(lines)))
    return path


class TestModuleOf:
    def test_a_nested_target_beats_the_module_it_sits_inside(self):
        """`libs/assetlib/cli` is a deeper prefix than `libs/assetlib`, and listed after it.

        So this fails the moment the rule becomes first-match rather than longest-match -- which is
        the whole reason `assetlib_cli` is nameable at all.
        """
        assert count_source.module_of('libs/assetlib/cli/main.cpp') == 'assetlib_cli'
        assert count_source.module_of('libs/assetlib/src/AssetStore.cpp') == 'assetlib'

    def test_a_sibling_sharing_a_name_prefix_is_not_swallowed(self):
        """`libs/assetlib` is a character-prefix of `libs/assetlib_structs` but not a path one."""
        assert count_source.module_of('libs/assetlib_structs/include/Mesh.h') == 'assetlib_structs'

    def test_a_backend_counts_as_its_library(self):
        assert count_source.module_of('libs/bgl/src/d3d12/Device.cpp') == 'bgl'
        assert count_source.module_of('libs/bgl/shaders/forward.slang') == 'bgl'

    def test_a_windows_path_resolves_the_same_as_a_posix_one(self):
        """`os.path.relpath` hands back `\\` separators on Windows, where this also runs."""
        assert count_source.module_of('libs\\assetlib\\cli\\main.cpp') == 'assetlib_cli'

    def test_an_uncovered_path_gets_its_own_name_rather_than_a_neighbour(self):
        assert count_source.module_of('libs/newlib/src/Thing.cpp') == count_source.UNCLASSIFIED
        assert count_source.module_of('cmake/enable_strict_compiler.cmake') == count_source.UNCLASSIFIED


class TestCollect:
    def test_the_two_breakdowns_partition_the_same_files(self, tmp_path):
        """Language and module totals must agree, or a file was dropped or counted twice."""
        root = str(tmp_path)
        write(root, '.gitignore', 0)
        write(root, 'libs/bgl/src/Device.cpp', 10)
        write(root, 'libs/bgl/shaders/forward.slang', 5)
        write(root, 'libs/bgl/tests/DeviceTests.cpp', 7)
        write(root, 'libs/assetlib/cli/main.cpp', 3)
        write(root, 'scripts/build.py', 4)
        write(root, 'somewhere/else/Thing.cpp', 2)

        by_language, by_module, totals = count_source.collect(root)

        for kind in ('src', 'test'):
            for field in ('files', 'lines'):
                summed = sum(t[kind][field] for t in by_module.values())
                assert summed == totals[kind][field]
                assert summed == sum(t[kind][field] for t in by_language.values())

    def test_an_unclassified_file_is_counted_and_named(self, tmp_path):
        root = str(tmp_path)
        write(root, '.gitignore', 0)
        write(root, 'somewhere/else/Thing.cpp', 2)

        _, by_module, _ = count_source.collect(root)

        assert by_module[count_source.UNCLASSIFIED]['src']['files'] == 1

    def test_a_test_file_is_charged_to_its_module_as_a_test(self, tmp_path):
        """The src/test split survives the module dimension: bgl's tests are bgl's, not a module of
        their own."""
        root = str(tmp_path)
        write(root, '.gitignore', 0)
        write(root, 'libs/bgl/src/Device.cpp', 10)
        write(root, 'libs/bgl/tests/DeviceTests.cpp', 7)

        _, by_module, _ = count_source.collect(root)

        assert by_module['bgl']['src'] == {'files': 1, 'lines': 10}
        assert by_module['bgl']['test'] == {'files': 1, 'lines': 7}
