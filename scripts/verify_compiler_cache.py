"""Prove the compiler cache returns the object the compiler would have made.

    python scripts/verify_compiler_cache.py --preset windows-ninja-msvc-dx12-debug

Run after a build. It takes a seeded sample of the build's translation units, deletes their
objects, rebuilds them -- which the cache must serve, and the script fails if it did not -- then
compiles the same commands again with the cache disabled and compares the two objects byte for
byte. A cache that returned a stale or foreign object fails here; one that merely missed fails
too, because a sample that was never served by the cache proves nothing about it.

The only bytes allowed to differ are the COFF header's timestamp, which the compiler stamps with
the time it ran.
"""

import argparse
import concurrent.futures
import json
import os
import random
import shutil
import struct
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg
from embed import HIT_COUNTERS, ccache_stats

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx")
BIGOBJ_SIGNATURE = b"\x00\x00\xff\xff"


def normalize_object(data):
    """A COFF object with the compile-time timestamp zeroed.

    A regular object keeps it at offset 4 and a /bigobj one, which starts with an anonymous-object
    header, at offset 8. Anything that is not a COFF object comes back unchanged, so a mismatch
    there still shows.
    """
    offset = 8 if data[:4] == BIGOBJ_SIGNATURE else 4
    if len(data) < offset + 4:
        return data
    return data[:offset] + struct.pack("<I", 0) + data[offset + 4:]


def pick_sample(entries, count, seed):
    """`count` compile entries that produce an object, the same ones for the same seed."""
    compiles = sorted(
        (e for e in entries
         if e.get("output", "").endswith(".obj") and e["file"].lower().endswith(SOURCE_SUFFIXES)),
        key=lambda e: e["output"])
    return random.Random(seed).sample(compiles, min(count, len(compiles)))


def compile_commands(ninja, binary_dir, env):
    out = subprocess.run([ninja, "-C", binary_dir, "-t", "compdb"], env=env, check=True,
                         capture_output=True, text=True).stdout
    return json.loads(out)


def read_objects(binary_dir, entries):
    result = {}
    for entry in entries:
        with open(os.path.join(binary_dir, entry["output"]), "rb") as f:
            result[entry["output"]] = f.read()
    return result


def hits_since(before, after):
    return sum(after.get(k, 0) - before.get(k, 0) for k in HIT_COUNTERS)


def compile_uncached(entry, binary_dir, env):
    uncached = dict(env, CCACHE_DISABLE="1")
    return subprocess.run(entry["command"], shell=True, cwd=entry.get("directory", binary_dir),
                          env=uncached, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", help="CMake preset whose build dir to verify (default: config.json).")
    parser.add_argument("--samples", type=int, default=48, help="Translation units to verify (default 48).")
    parser.add_argument("--seed", type=int, default=0, help="Sampling seed (default 0).")
    args = parser.parse_args()

    preset = cfg.preset(args.preset)
    generator = ct.generator_of(preset)
    env, _ = cfg.build_env(generator)
    ninja = cfg.find_ninja(env)
    binary_dir = ct.binary_dir_of(preset)
    ccache = shutil.which("ccache")
    if not (ninja and binary_dir and ccache):
        print("error: needs ninja, a configured build dir and ccache on PATH.", file=sys.stderr)
        return 1

    sample = pick_sample(compile_commands(ninja, binary_dir, env), args.samples, args.seed)
    if not sample:
        print("error: the build has no compile commands to sample.", file=sys.stderr)
        return 1

    for entry in sample:
        os.remove(os.path.join(binary_dir, entry["output"]))

    before = ccache_stats(ccache)
    rebuilt = subprocess.run([ninja, "-C", binary_dir] + [e["output"] for e in sample], env=env)
    if rebuilt.returncode:
        print("error: rebuilding the sampled objects failed.", file=sys.stderr)
        return rebuilt.returncode
    served = hits_since(before or {}, ccache_stats(ccache) or {})
    if served < len(sample):
        print(f"FAIL: the cache served {served} of {len(sample)} sampled compiles; "
              f"a sample it did not serve proves nothing.", file=sys.stderr)
        return 1

    from_cache = read_objects(binary_dir, sample)

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        compiled = list(pool.map(lambda e: compile_uncached(e, binary_dir, env), sample))

    mismatches = []
    for entry, proc in zip(sample, compiled):
        if proc.returncode:
            print(f"error: {entry['file']} did not compile uncached:\n{proc.stdout}{proc.stderr}",
                  file=sys.stderr)
            return 1
        fresh = read_objects(binary_dir, [entry])[entry["output"]]
        if normalize_object(fresh) != normalize_object(from_cache[entry["output"]]):
            mismatches.append(entry["file"])

    if mismatches:
        print(f"FAIL: {len(mismatches)} of {len(sample)} cached objects differ from a fresh compile:",
              file=sys.stderr)
        for name in mismatches:
            print(f"  {name}", file=sys.stderr)
        return 1

    print(f"Compiler cache verified: {len(sample)} objects served from the cache are byte-identical "
          f"to a fresh compile (seed {args.seed}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
