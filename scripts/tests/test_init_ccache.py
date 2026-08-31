"""Keeping Spotlight out of the compiler cache's store.

The store is gigabytes of small files rewritten constantly, which is what `ws init` already drops
`.metadata_never_index` into every checkout's `build/` to keep an indexer away from. A cache that
speeds the compiler up while feeding the indexer is not a saving -- and the symptom is a slow
machine rather than a slow build, so nothing about the build would ever point at it.

`ccache --show-config` is stubbed with a shell script rather than mocked: the parsing of its output
is the part that breaks when ccache changes its format, so the test drives the real code path.
"""

import os
import stat

import pytest

import init


def fake_ccache(directory, store):
    """A stand-in for the ccache binary that reports `store` as its cache_dir."""
    path = os.path.join(directory, "ccache")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(f'#!/bin/sh\necho "(default) cache_dir = {store}"\n')
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
    return path


darwin_only = pytest.mark.skipif(init.sys.platform != "darwin",
                                 reason="Spotlight, and this marker, are macOS only")


@darwin_only
def test_a_store_that_does_not_exist_yet_is_created_and_marked(tmp_path):
    """The fresh machine is the case that matters: ccache has never run, so the store is absent,
    and `just init` is the one chance to mark it before it fills up."""
    store = str(tmp_path / "never-run" / "ccache")

    init.exclude_ccache_from_spotlight(fake_ccache(str(tmp_path), store))

    assert os.path.isdir(store)
    assert os.path.exists(os.path.join(store, ".metadata_never_index"))


@darwin_only
def test_marking_twice_is_harmless(tmp_path):
    """`just init` is documented as re-runnable, so this runs on every invocation."""
    store = str(tmp_path / "store")
    ccache = fake_ccache(str(tmp_path), store)

    init.exclude_ccache_from_spotlight(ccache)
    init.exclude_ccache_from_spotlight(ccache)

    assert os.path.exists(os.path.join(store, ".metadata_never_index"))


@darwin_only
def test_a_ccache_that_cannot_be_run_is_not_fatal(tmp_path):
    """Nothing here is worth failing `just init` over -- an indexed store still caches correctly."""
    init.exclude_ccache_from_spotlight(str(tmp_path / "not-a-program"))


@darwin_only
def test_output_without_a_cache_dir_line_is_left_alone(tmp_path):
    """A future ccache that renames the key must write no marker rather than guess a path."""
    path = os.path.join(str(tmp_path), "ccache")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write('#!/bin/sh\necho "(default) max_size = 5.0G"\n')
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)

    init.exclude_ccache_from_spotlight(path)

    assert os.listdir(tmp_path) == ["ccache"]
