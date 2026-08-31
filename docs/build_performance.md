# Build performance

Measure before changing anything — `just build --time`.

## Measuring

```bash
just build --time              # build, then report where the time went
python3 scripts/build_timing.py --top 30   # report the last build again, without rebuilding
python3 scripts/build_timing.py --all --json
```

It reads `.ninja_log` in the build directory, which ninja writes for free — one line per edge, with
its start and end. Nothing is instrumented and no wrapper is involved, so the numbers are the build
that actually ran.

**Wall and CPU are different claims.** Wall is how long the build took and parallelism decides it;
CPU is the sum of every edge's duration, which is the work the machine did. A change that removes
work moves CPU. On an idle 12-core machine a clean build is around a minute of wall against twelve
minutes of CPU, so wall is nearly all parallelism — and CPU is what matters, because CPU is what
several checkouts contend for.

The rollup is **by target as well as by module**, and the target grain is the one that matters: a
module is several targets, and a change that helps a library while hurting its test suite nets out to
nothing at module grain. That is not hypothetical — it is how a PCH regression hid here once.

Only object files and PCHs count as compile time. moc, IDL codegen, asset staging and linking are
real build time but move for their own reasons, and a total that mixed them would hide the thing
being measured.

**A measurement on a loaded machine is not a measurement.** Sibling checkouts building at the same
time inflate everything by half again or more. Certify a comparison against a target the change did
not touch — if `core` and `examples` moved, the machine moved, not the code.

### Where the parsing actually goes

`.ninja_log` says what each edge cost; ninja's dependency database says *why*. This lists what a
target's TUs read that its PCH does not already cover, which is the number a PCH change moves:

```bash
ninja -C build/<preset> -t deps > /tmp/deps.txt
```

Each `.o` entry lists every header that TU read, and the target's `cmake_pch.hxx.pch` entry lists
what the PCH covers; the difference is what is being re-parsed per file.
