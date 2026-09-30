"""What `just embed` reads from the executable to say which engine libraries it loads.

The check itself runs only at the end of a real embed build, so these pin the two parsers and the
comparison on the tools' actual output shapes, where a regression would otherwise pass silently.
"""

import embed

OTOOL = """\
/repo/build/embed/bin/bernini_embed:
\t@rpath/libbgl.dylib (compatibility version 0.0.0, current version 0.0.0)
\t@rpath/libcore_process.dylib (compatibility version 0.0.0, current version 0.0.0)
\t/System/Library/Frameworks/Metal.framework/Versions/A/Metal (compatibility version 1.0.0, current version 368.12.0)
\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1800.101.0)
"""

DUMPBIN = """\
Dump of file build\\embed\\bin\\bernini_embed.exe

File Type: EXECUTABLE IMAGE

  Image has the following dependencies:

    core_process.dll
    bgl.dll
    KERNEL32.dll

  Summary

        1000 .data
"""


def test_otool_names_every_library_but_the_binary_itself():
    assert embed.parse_otool(OTOOL) == {"bgl", "core_process", "Metal", "c++"}


def test_dumpbin_names_every_dll():
    assert embed.parse_dumpbin(DUMPBIN) == {"core_process", "bgl", "KERNEL32"}


def test_a_library_the_build_expects_but_the_executable_lacks_is_reported():
    linked = embed.parse_otool(OTOOL) - {"bgl"}
    wrong = embed.linkage_mismatches(linked, {"bgl": True, "core_process": True})
    assert len(wrong) == 1 and wrong[0].startswith("bgl is not")


def test_a_library_the_build_says_is_static_but_the_executable_loads_is_reported():
    wrong = embed.linkage_mismatches(embed.parse_otool(OTOOL),
                                     {"bgl": False, "core_process": True})
    assert len(wrong) == 1 and wrong[0].startswith("bgl is a library")


def test_a_match_reports_nothing():
    assert embed.linkage_mismatches(embed.parse_dumpbin(DUMPBIN),
                                    {"bgl": True, "core_process": True}) == []


def test_a_static_renderer_expects_neither_library():
    assert embed.expected_linkage({"BERNINI_RENDERER_LIBRARY_TYPE": "STATIC"}) == {
        "bgl": False, "core_process": False}


def test_a_shared_renderer_expects_both():
    assert embed.expected_linkage({"BERNINI_RENDERER_LIBRARY_TYPE": "SHARED"}) == {
        "bgl": True, "core_process": True}
