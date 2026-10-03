#!/usr/bin/env bash
# Runs bgl_tests under D3D12 GPU-based validation, sharded, every shard at once.
#
# Each shard runs from a directory of its own holding hard links to bgl_tests.exe and the DLLs beside
# it. bgpu.log, the Agility SDK (D3D12SDKPath ".\") and crash logs resolve beside the executable, so
# every shard writes its own log and its own crash logs, and none truncates another's. The working
# directory stays <bin-dir>, where assets/, shaders/ and shadercache/ resolve.
#
# Usage: gbv_shards.sh <bin-dir> <out-dir> [catch2 test spec]
#   <bin-dir>  the build's runtime dir: build/ninja-clang-gbv/bin, or the debug bin for the debug-only files
#   <out-dir>  shard<N>.out, shard<N>.bgpu.log, summary.txt, slowest.txt and events.log land here
#   spec       optional, one argument; commas are OR: "[#CullInstances_test],[#DebugAssert_test]"
#   GBV_SHARDS shard count (default 4)   GBV_SEED  rng seed (default random; in summary.txt)
#
# The order is random under a recorded seed: it spreads one file's slow cases over the shards, and
# `bgl_tests.exe <same args> --list-tests` prints a shard's cases in exactly the order it ran them.
#
# events.log gets one line per problem and nothing else -- it is what a Monitor tails:
#   FAIL shard<N> rc=<n> after "<last completed case>"   the shard's process failed (22: abort, often a TDR)
#   GBV shard<N> <message>                               a deduplicated D3D12 / validation / error log line
#   CRASH shard<N> <crash log path>
# and a final DONE line.

set -u

bin="$(cd "$1" && pwd)"
out="$2"
spec="${3:-}"
shards="${GBV_SHARDS:-4}"
seed="${GBV_SEED:-$((RANDOM * 32768 + RANDOM))}"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
: > "$out/events.log"
echo "seed=$seed shards=$shards spec=${spec:-<all>}" > "$out/summary.txt"

cd "$bin" || exit 1
start=$(date +%s)

for ((n = 0; n < shards; n++)); do
	dir=".gbv-shard$n"
	rm -rf "$dir"
	mkdir "$dir"
	for f in bgl_tests.exe *.dll; do ln "$f" "$dir/$f"; done

	tmp="$out/tmp$n"
	rm -rf "$tmp"
	mkdir -p "$tmp"
	wtmp="$(cygpath -w "$tmp")"

	(
		s=$(date +%s)
		args=(-# --order rand --rng-seed "$seed" --shard-count "$shards" --shard-index "$n" -d yes --gpu-validation)
		[ -n "$spec" ] && args=("$spec" "${args[@]}")
		TMP="$wtmp" TEMP="$wtmp" TMPDIR="$wtmp" "./$dir/bgl_tests.exe" "${args[@]}" > "$out/shard$n.out" 2>&1
		rc=$?
		secs=$(( $(date +%s) - s ))

		[ -f "$dir/bgpu.log" ] && cp "$dir/bgpu.log" "$out/shard$n.bgpu.log"

		# rc 2: no case matched on this backend. rc 4: every case skipped itself. Neither is a failure.
		if [ "$rc" -ne 0 ] && [ "$rc" -ne 2 ] && [ "$rc" -ne 4 ]; then
			last="$(grep -E '^[0-9.]+ s: ' "$out/shard$n.out" | tail -1 | sed -E 's/^[0-9.]+ s: //')"
			echo "FAIL shard$n rc=$rc after \"${last:-<none>}\" -- $out/shard$n.out" >> "$out/events.log"
		fi

		if [ -f "$out/shard$n.bgpu.log" ]; then
			# Strip timestamps, thread ids and addresses so one message repeated per draw is one event.
			grep -E "\[D3D12\]|GPU-BASED VALIDATION|\[error\]|\[critical\]" "$out/shard$n.bgpu.log" \
				| grep -v "This API cannot be called on a closed command list" \
				| sed -E 's/^\[[^]]*\] \[thread [0-9]+\] //; s/0x[0-9A-Fa-f]+/0x_/g; s/Command List: .*//' \
				| cut -c1-400 | sort -u \
				| sed "s/^/GBV shard$n /" >> "$out/events.log"
		fi

		for crash in "$dir"/bgl_tests_crash_*.log; do
			[ -f "$crash" ] || continue
			cp "$crash" "$out/"
			echo "CRASH shard$n $out/$(basename "$crash")" >> "$out/events.log"
		done

		echo "shard$n rc=$rc ${secs}s $(grep -E 'test cases|All tests passed' "$out/shard$n.out" | tail -1)" >> "$out/summary.txt"
	) &
done

wait

echo "total $(( $(date +%s) - start ))s" >> "$out/summary.txt"
# Case durations, slowest first: where a validated run's time goes, and what SkipUnderGpuValidation could cut.
cat "$out"/shard*.out | grep -E '^[0-9.]+ s: ' | sort -rn | head -25 > "$out/slowest.txt"
echo "DONE" >> "$out/events.log"
