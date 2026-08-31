#!/usr/bin/env python3
"""Where a build spent its time, read back from ninja's own log.

ninja records one line per edge it ran -- start and end in milliseconds, and the output path -- in
`.ninja_log` inside the build directory. That is a complete, free record of every build ever run in
that directory, so measuring a change to compile time needs no instrumentation and no wrapper: build,
then read the log back.

Two numbers matter and they are not the same. **Wall** is how long the build took, which parallelism
decides; **CPU** is the sum of every edge's duration, which is the work the machine actually did. A
change that removes work moves CPU. A change that only schedules better moves wall alone.

The log accumulates across invocations and each one's timestamps restart at zero, so the entries are
split back into builds before anything is totalled -- see `split_invocations`.
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import util.cmake_tools as ct
import util.config as cfg
from count_source import module_of

LOG_NAME = ".ninja_log"

# The five-field line below has been the format since v5 and still is at v7, so the guard is on the
# shape rather than the number: an allowlist of versions breaks on a ninja that changed nothing, and
# a version that changed the shape is caught by the shape.
MIN_VERSION = 5

# What a compile *produces*, rather than a list of what the other edges are. The build also stages
# assets, generates IDL, runs moc and links, and those are named by no fixed set of suffixes -- an
# exclusion list silently counts the next kind of generated file as compile time.
COMPILE_SUFFIXES = (".o", ".obj", ".pch", ".gch")

# CMake puts every object under `<dir>/CMakeFiles/<target>.dir/`, which is the only place a target's
# name appears in an output path. Worth having separately from the module rollup: a module is
# several targets, and a change that helps a library while hurting its test suite nets out to
# nothing at that grain.
TARGET_DIR = re.compile(r"CMakeFiles/(?P<target>[^/]+)\.dir/")


class LogError(Exception):
    """The log is absent or in a format this cannot read."""


def log_path(binary_dir):
    return os.path.join(binary_dir, LOG_NAME)


def _relative(output, binary_dir):
    """`output` shortened against the build directory, else the repo root, else left as it is.

    ninja records an output either way, and codegen that writes into the source tree (the IDL's
    `.slang` copies) lands outside the build directory altogether -- so the repo root is the second
    chance, and it is what lets `module_of` recognise those paths at all.
    """
    normalised = output.replace("\\", "/")
    if not normalised.startswith("/") and not re.match(r"^[A-Za-z]:", normalised):
        return normalised

    for root in (binary_dir, ct.REPO_ROOT):
        if not root:
            continue
        prefix = os.path.abspath(root).replace("\\", "/").rstrip("/") + "/"
        if normalised.startswith(prefix):
            return normalised[len(prefix):]
    return normalised


def parse(path, binary_dir=None):
    """Every edge in a `.ninja_log`, once each, in the order ninja appended them.

    @param path Path to the `.ninja_log`.
    @param binary_dir Build directory, used to relativise absolutely-recorded outputs.
    @return List of {"start", "end", "output"}, milliseconds and build-dir-relative path.
    @throws LogError if the file is absent, headerless, or older than the format this reads.
    """
    if not os.path.exists(path):
        raise LogError(f"no {LOG_NAME} in {os.path.dirname(path) or '.'} -- run a build first")

    entries = []
    seen = set()

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        header = handle.readline().strip()
        if not header.startswith("# ninja log v"):
            raise LogError(f"{path} does not start with a ninja log header")

        try:
            version = int(header.rsplit("v", 1)[1])
        except (IndexError, ValueError):
            raise LogError(f"could not read a version out of '{header}'")

        if version < MIN_VERSION:
            raise LogError(f"{LOG_NAME} is v{version}; this reads v{MIN_VERSION} and later")

        for line in handle:
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 5:
                raise LogError(f"{LOG_NAME} (v{version}) has {len(fields)}-field lines, not the 5 "
                               f"this reads -- the format changed")
            try:
                start, end = int(fields[0]), int(fields[1])
            except ValueError:
                continue

            # An edge is one unit of work however many outputs it declares, and the command hash is
            # what identifies it -- keying on the output would count a multi-output edge twice.
            key = (start, end, fields[4])
            if key in seen:
                continue
            seen.add(key)

            entries.append({"start": start, "end": end,
                            "output": _relative(fields[3], binary_dir)})

    return entries


def split_invocations(entries):
    """`entries` grouped into the builds that produced them, oldest first.

    Every ninja invocation appends to the same log and times its edges from its own start, so the
    timestamps restart at zero on each one. Within a build ninja appends in completion order, so
    `end` never decreases; a drop is therefore the boundary between two builds. That is the same
    heuristic ninjatracing uses, and it is the only signal in the file -- v5 records no build id.

    A build whose every edge was up to date appends nothing, so it leaves no invocation here at all.
    """
    invocations = []
    current = []
    previous_end = None

    for entry in entries:
        if previous_end is not None and entry["end"] < previous_end:
            invocations.append(current)
            current = []
        current.append(entry)
        previous_end = entry["end"]

    if current:
        invocations.append(current)
    return invocations


def is_compile(output):
    return output.endswith(COMPILE_SUFFIXES)


def target_of(output):
    """The CMake target an output belongs to, or None when the path does not name one."""
    found = TARGET_DIR.search(output)
    return found.group("target") if found else None


def summarise(entries):
    """Totals, per-module rollup and the slowest edges for one invocation's `entries`."""
    edges = []
    for entry in entries:
        duration = entry["end"] - entry["start"]
        edges.append({
            "output": entry["output"],
            "ms": duration,
            "module": module_of(entry["output"]),
            "target": target_of(entry["output"]),
            "compile": is_compile(entry["output"]),
        })

    compiles = [e for e in edges if e["compile"]]

    by_module, by_target = {}, {}
    for edge in compiles:
        row = by_module.setdefault(edge["module"], {"edges": 0, "ms": 0})
        row["edges"] += 1
        row["ms"] += edge["ms"]

        row = by_target.setdefault(edge["target"] or "(no target)", {"edges": 0, "ms": 0})
        row["edges"] += 1
        row["ms"] += edge["ms"]

    return {
        # Edges overlap, so wall is the span they cover rather than the sum of their durations.
        "wall_ms": max(e["end"] for e in entries) - min(e["start"] for e in entries) if entries else 0,
        "cpu_ms": sum(e["ms"] for e in edges),
        "compile_cpu_ms": sum(e["ms"] for e in compiles),
        "edges": len(edges),
        "compiles": len(compiles),
        "by_module": by_module,
        "by_target": by_target,
        "slowest": sorted(edges, key=lambda e: -e["ms"]),
    }


def fmt_ms(ms):
    """Milliseconds as the coarsest unit that still says something: 940ms, 12.4s, 3m41s."""
    if ms < 1000:
        return f"{ms}ms"
    seconds = ms / 1000.0
    if seconds < 60:
        return f"{seconds:.1f}s"
    return f"{int(seconds) // 60}m{int(seconds) % 60:02d}s"


def report(summary, top, stream=sys.stdout):
    write = stream.write
    write("=" * 66 + "\n")
    write(" BUILD TIMING\n")
    write("=" * 66 + "\n\n")

    write(f"  wall            {fmt_ms(summary['wall_ms'])}\n")
    write(f"  cpu (all edges) {fmt_ms(summary['cpu_ms'])}\n")
    write(f"  cpu (compiles)  {fmt_ms(summary['compile_cpu_ms'])}"
          f"   over {summary['compiles']} of {summary['edges']} edges\n")

    if summary["wall_ms"]:
        write(f"  parallelism     {summary['cpu_ms'] / summary['wall_ms']:.1f}x\n")

    if summary["by_module"]:
        write("\n  Compile time by module\n")
        write("  " + "-" * 44 + "\n")
        write(f"  {'Module':<22}{'Files':>8}{'CPU':>12}\n")
        write("  " + "-" * 44 + "\n")
        for name, row in sorted(summary["by_module"].items(), key=lambda kv: -kv[1]["ms"]):
            write(f"  {name:<22}{row['edges']:>8}{fmt_ms(row['ms']):>12}\n")

    if summary["by_target"]:
        write("\n  Compile time by target\n")
        write("  " + "-" * 44 + "\n")
        write(f"  {'Target':<22}{'Files':>8}{'CPU':>12}\n")
        write("  " + "-" * 44 + "\n")
        for name, row in sorted(summary["by_target"].items(), key=lambda kv: -kv[1]["ms"]):
            write(f"  {name:<22}{row['edges']:>8}{fmt_ms(row['ms']):>12}\n")

    slowest = summary["slowest"][:top]
    if slowest:
        write(f"\n  Slowest {len(slowest)} edges\n")
        write("  " + "-" * 62 + "\n")
        for edge in slowest:
            write(f"  {fmt_ms(edge['ms']):>9}  {edge['output']}\n")

    write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", help="CMake preset whose build dir to read (default: config.json).")
    parser.add_argument("--build-dir", help="Build directory to read, overriding --preset.")
    parser.add_argument("--top", type=int, default=15, help="How many of the slowest edges to list.")
    parser.add_argument("--all", action="store_true",
                        help="Summarise every invocation in the log, oldest first, not just the last.")
    parser.add_argument("--json", action="store_true", help="Emit the summary as JSON.")
    args = parser.parse_args(argv)

    binary_dir = args.build_dir or ct.binary_dir_of(cfg.preset(args.preset))
    if not binary_dir:
        print("error: could not resolve a build directory; pass --build-dir.", file=sys.stderr)
        return 1

    try:
        entries = parse(log_path(binary_dir), binary_dir)
    except LogError as err:
        print(f"error: {err}", file=sys.stderr)
        return 1

    invocations = split_invocations(entries)
    if not invocations:
        print(f"error: {LOG_NAME} records no completed edges.", file=sys.stderr)
        return 1

    chosen = invocations if args.all else invocations[-1:]
    summaries = [summarise(inv) for inv in chosen]

    if args.json:
        for summary in summaries:
            summary["slowest"] = summary["slowest"][:args.top]
        json.dump(summaries if args.all else summaries[0], sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0

    for index, summary in enumerate(summaries):
        if args.all:
            print(f"\n--- invocation {index + 1} of {len(summaries)} ---")
        report(summary, args.top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
