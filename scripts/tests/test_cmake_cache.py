"""read_cache: what `just embed --package` learns about the engine build it consumes."""

import util.cmake_tools as ct


def test_a_cache_entry_is_read_by_name_whatever_its_type(tmp_path):
    (tmp_path / "CMakeCache.txt").write_text(
        "# This is the CMakeCache file.\n"
        "//Choose the type of build\n"
        "CMAKE_BUILD_TYPE:STRING=Release\n"
        "VCPKG_INSTALLED_DIR:PATH=/ws/.ws/vcpkg\n"
        "BERNINI_PROFILING:BOOL=ON\n"
        "EMPTY:STRING=\n"
        "COMPILE_FLAGS:STRING=-DA=1\n",
        encoding="utf-8")

    cache = ct.read_cache(str(tmp_path))

    assert cache["CMAKE_BUILD_TYPE"] == "Release"
    assert cache["VCPKG_INSTALLED_DIR"] == "/ws/.ws/vcpkg"
    assert cache["EMPTY"] == ""
    assert cache["COMPILE_FLAGS"] == "-DA=1"
    assert "This is the CMakeCache file." not in "".join(cache)


def test_an_unconfigured_build_dir_reads_as_empty(tmp_path):
    assert ct.read_cache(str(tmp_path / "missing")) == {}
