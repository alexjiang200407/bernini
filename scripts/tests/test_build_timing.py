"""Reading a build's time back out of ninja's log.

The cases pin the three things that make the totals wrong rather than merely imprecise: an edge
counted twice because ninja logged it once per output, work that is not a compile counted as
compile time, and two builds in one log summed as if they were one.
"""

import os

import pytest

import build_timing as bt

HEADER = "# ninja log v7\n"


def write_log(path, rows, header=HEADER):
    """`rows` of (start, end, output, command_hash) as a ninja log at `path`."""
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header)
        for start, end, output, digest in rows:
            handle.write(f"{start}\t{end}\t0\t{output}\t{digest}\n")
    return str(path)


def test_an_edge_logged_once_per_output_is_one_edge(tmp_path):
    """ninja writes a line per output, so a multi-output edge must not be counted twice."""
    log = write_log(tmp_path / ".ninja_log", [
        (0, 100, "bin/.assets.stamp", "aaaa"),
        (0, 100, "/abs/bin/.assets.stamp", "aaaa"),
        (100, 300, "libs/core/CMakeFiles/core.dir/src/A.cpp.o", "bbbb"),
    ])

    entries = bt.parse(log)

    assert len(entries) == 2
    assert bt.summarise(entries)["cpu_ms"] == 300


def test_only_object_files_count_as_compile_time(tmp_path):
    """moc, codegen and linking are real build time but not compile time, and move separately."""
    log = write_log(tmp_path / ".ninja_log", [
        (0, 1000, "apps/editor/editor_lib_autogen/timestamp", "aaaa"),
        (0, 500, "libs/bgl_common/shaders/src/idl/Entry.slang", "bbbb"),
        (0, 200, "bin/editor", "cccc"),
        (0, 300, "apps/editor/CMakeFiles/editor_lib.dir/src/MainWindow.cpp.o", "dddd"),
        (0, 400, "libs/core/CMakeFiles/core.dir/cmake_pch.hxx.pch", "eeee"),
    ])

    summary = bt.summarise(bt.parse(log))

    assert summary["cpu_ms"] == 2400
    assert summary["compiles"] == 2
    assert summary["compile_cpu_ms"] == 700


def test_two_builds_in_one_log_are_split(tmp_path):
    """Each invocation times from zero, so a drop in `end` is where the next build began."""
    log = write_log(tmp_path / ".ninja_log", [
        (0, 100, "a.cpp.o", "aaaa"),
        (100, 900, "b.cpp.o", "bbbb"),
        (0, 50, "c.cpp.o", "cccc"),
        (50, 200, "d.cpp.o", "dddd"),
    ])

    invocations = bt.split_invocations(bt.parse(log))

    assert [len(inv) for inv in invocations] == [2, 2]
    assert bt.summarise(invocations[-1])["wall_ms"] == 200


def test_wall_is_the_span_and_cpu_is_the_sum(tmp_path):
    """Overlapping edges are the normal case; conflating the two numbers hides all parallelism."""
    log = write_log(tmp_path / ".ninja_log", [
        (0, 1000, "a.cpp.o", "aaaa"),
        (0, 1000, "b.cpp.o", "bbbb"),
        (0, 1000, "c.cpp.o", "cccc"),
    ])

    summary = bt.summarise(bt.parse(log))

    assert summary["wall_ms"] == 1000
    assert summary["cpu_ms"] == 3000


def test_outputs_are_attributed_to_a_module(tmp_path):
    """The per-module rollup is the whole point; an unrecognised path must not join a neighbour."""
    log = write_log(tmp_path / ".ninja_log", [
        (0, 100, "libs/bgl_extended/CMakeFiles/bgl_extended_objects.dir/src/A.cpp.o", "aaaa"),
        (0, 200, "apps/editor/CMakeFiles/editor_lib.dir/src/B.cpp.o", "bbbb"),
        (0, 300, "_deps/qtnodes-build/CMakeFiles/QtNodes.dir/src/C.cpp.o", "cccc"),
    ])

    by_module = bt.summarise(bt.parse(log))["by_module"]

    assert by_module["bgl_extended"]["ms"] == 100
    assert by_module["editor"]["ms"] == 200
    assert by_module["(unclassified)"]["ms"] == 300


def test_an_absolute_output_inside_the_build_dir_is_relativised(tmp_path):
    """Without this the path joins no module and the rollup silently loses it."""
    binary_dir = tmp_path / "build" / "macos-metal-debug"
    os.makedirs(binary_dir)
    log = write_log(binary_dir / ".ninja_log", [
        (0, 100, str(binary_dir / "libs/bgl_extended/CMakeFiles/bgl_extended_objects.dir/src/A.cpp.o"), "aaaa"),
    ])

    entries = bt.parse(log, str(binary_dir))

    assert entries[0]["output"] == "libs/bgl_extended/CMakeFiles/bgl_extended_objects.dir/src/A.cpp.o"


def test_a_log_with_no_header_is_refused(tmp_path):
    log = tmp_path / ".ninja_log"
    log.write_text("0\t100\t0\ta.cpp.o\taaaa\n", encoding="utf-8")

    with pytest.raises(bt.LogError):
        bt.parse(str(log))


def test_a_changed_line_shape_is_refused_rather_than_misread(tmp_path):
    """A future format with different fields must not be summarised as though it parsed."""
    log = tmp_path / ".ninja_log"
    log.write_text(HEADER + "0\t100\t0\ta.cpp.o\taaaa\textra\n", encoding="utf-8")

    with pytest.raises(bt.LogError):
        bt.parse(str(log))


def test_a_missing_log_says_to_build_first(tmp_path):
    with pytest.raises(bt.LogError, match="run a build first"):
        bt.parse(str(tmp_path / ".ninja_log"))


def test_outputs_are_attributed_to_a_target(tmp_path):
    """A module is several targets, and a change that helps a library while hurting its suite nets
    out to nothing at module grain -- which is how a regression hid here once."""
    log = write_log(tmp_path / ".ninja_log", [
        (0, 100, "libs/bgl_extended/CMakeFiles/bgl_extended_objects.dir/src/A.cpp.o", "aaaa"),
        (0, 200, "libs/bgl_extended/CMakeFiles/bgl_extended_tests.dir/tests/src/B.cpp.o", "bbbb"),
        (0, 300, "bin/editor", "cccc"),
    ])

    summary = bt.summarise(bt.parse(log))

    assert summary["by_target"]["bgl_extended_objects"]["ms"] == 100
    assert summary["by_target"]["bgl_extended_tests"]["ms"] == 200
    assert summary["by_module"]["bgl_extended"]["ms"] == 300
    assert "(no target)" not in summary["by_target"]
