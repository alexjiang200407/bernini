"""Nothing bgpu's Metal backend autoreleases is left without a pool to drain it.

A compute client beside the renderer holds no autorelease pool of its own, so every Metal entry point
in bgpu pushes one (libs/bgpu/CLAUDE.md, Metal). A forgotten one leaks silently -- the renderer's net
hides it -- so this runs the whole of bgpu_tests, which drives the RHI with no renderer in the process,
under the runtime's own check, and fails on anything of ours it names. The coverage is the suite's:
RhiEntryPoints_test is there to reach every factory and destroy at least once.

Only on a Mac, and only once bgpu_tests is built.
"""

import os
import re
import subprocess
import sys

import pytest

import util.cmake_tools as ct
import util.config as cfg

# Metal autoreleases the device itself on its own completion threads, as a finished command buffer
# releases what it held -- after any code here has returned, so no pool here can reach it, and the
# device is never freed. Every other class is ours.
_MISSING = re.compile(r"MISSING POOLS: \((0x[0-9a-f]+)\) Object 0x[0-9a-f]+ of class (\w+)")
_METAL_DEVICE = re.compile(r"Device$")


def _bgpu_tests():
    for build_dir in ct.find_build_dirs(cfg.build_dir()):
        for target in ct.load_targets(build_dir):
            if target["name"] != "bgpu_tests":
                continue
            for path in target["artifacts"]:
                if os.path.isfile(path):
                    return path
    return None


@pytest.mark.skipif(sys.platform != "darwin", reason="autorelease pools are Metal's")
def test_nothing_of_ours_is_autoreleased_without_a_pool():
    exe = _bgpu_tests()
    if exe is None:
        pytest.skip("bgpu_tests is not built")

    result = subprocess.run(
        [exe],
        cwd=os.path.dirname(exe),
        env=dict(os.environ, OBJC_DEBUG_MISSING_POOLS="YES"),
        capture_output=True,
        text=True,
        timeout=600,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    ours = [m.group(2) for m in _MISSING.finditer(result.stderr) if not _METAL_DEVICE.search(m.group(2))]
    assert ours == [], f"autoreleased with no pool in place: {sorted(set(ours))}"
