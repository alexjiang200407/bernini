"""bgpu's Metal entry points drain what they autorelease (scripts/util/autorelease_scope.py).

Three things are pinned here: what the textual rule accepts and refuses, that the tree keeps it, and
-- on a Mac with bgpu_tests built -- that nothing on the calling thread is autoreleased with no pool
in place, which is what the rule exists for and what a text check cannot see.
"""

import os
import re
import subprocess
import sys

import pytest

import util.autorelease_scope as scope
import util.cmake_tools as ct
import util.config as cfg

POOLED = """namespace bgpu
{
	BufferHandle
	ResourceManager::CreateRawBuffer(const RawViewDesc& desc) noexcept
	{
		const auto pool = ScopeAutoreleasePool();
		return {};
	}
}
"""

UNPOOLED = """namespace bgpu
{
	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		return {};
	}
}
"""

EXEMPT = """namespace bgpu
{
	// no-pool: a slot lookup; sends no message that autoreleases.
	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return true;
	}
}
"""

EMPTY_REASON = """namespace bgpu
{
	// no-pool:
	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return true;
	}
}
"""

EXEMPT_FILE = """// no-pool-file: every call records between Open and Close, inside the pool Open pushes.
namespace bgpu
{
	void
	CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept
	{
	}
}
"""


def test_a_pooled_entry_point_passes():
    assert scope.unpooled_in_source("a.cpp", POOLED) == []


def test_an_entry_point_without_a_pool_is_named():
    found = scope.unpooled_in_source("a.cpp", UNPOOLED)
    assert [(u.line, u.name) for u in found] == [(4, "ResourceManager::CreateTexture")]


def test_an_exemption_needs_its_reason():
    assert scope.unpooled_in_source("a.cpp", EXEMPT) == []
    assert [u.name for u in scope.unpooled_in_source("a.cpp", EMPTY_REASON)] == [
        "ResourceManager::ValidBufferHandle"
    ]


def test_a_file_exemption_covers_every_method_in_it():
    assert scope.unpooled_in_source("a.cpp", EXEMPT_FILE) == []


def test_the_tree_keeps_the_rule():
    found = scope.unpooled(ct.REPO_ROOT)
    assert found == [], "\n".join(str(u) for u in found)


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

    env = dict(os.environ, OBJC_DEBUG_MISSING_POOLS="YES")
    result = subprocess.run(
        [exe, "[compute],[teardown]"],
        cwd=os.path.dirname(exe),
        env=env,
        capture_output=True,
        text=True,
        timeout=300,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    ours = [m.group(2) for m in _MISSING.finditer(result.stderr) if not _METAL_DEVICE.search(m.group(2))]
    assert ours == [], f"autoreleased with no pool in place: {sorted(set(ours))}"
