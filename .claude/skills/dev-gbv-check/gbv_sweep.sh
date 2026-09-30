#!/usr/bin/env bash
# Runs bgl_tests under D3D12 GPU-based validation, one source file per process, in series.
#
# One file per process, never sharded: a TDR resets the adapter and kills every process on it, so a
# parallel run blames the wrong cases; and every process truncates bin/bgpu.log on start, so only a
# serial run leaves a log per file.
#
# Usage: gbv_sweep.sh <bin-dir> <out-dir> [TestFile_test ...]
#   <bin-dir>  the build's runtime dir (just exes --target bgl_tests, its dirname)
#   <out-dir>  where <file>.out, <file>.bgpu.log, summary.txt and events.log land
#   files      optional; default is every libs/bgl/tests/src/*_test.cpp
#
# events.log gets one line per problem and nothing else -- it is what a Monitor tails:
#   FAIL <file> rc=<n> <last lines>        the file's process failed (22 = abort, usually a TDR)
#   GBV <file> <message>                   a deduplicated D3D12 / GPU-based validation message
#   CRASH <file> <crash log path>          a crash log the file's process wrote
#   LOG <file> <path>: <error line>        an error line in any other .log the run wrote
# and a final DONE line.

set -u

bin="$1"
out="$2"
shift 2

repo="$(cd "$bin" && git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$repo" ]; then
	repo="$(cd "$(dirname "$0")/../../.." && pwd)"
fi

if [ "$#" -gt 0 ]; then
	files="$*"
else
	files="$(ls "$repo"/libs/bgl/tests/src/*_test.cpp | xargs -n1 basename | sed 's/\.cpp$//')"
fi

mkdir -p "$out"
: > "$out/summary.txt"
touch "$out/events.log"

cd "$bin" || exit 1

# A hung CPU side would otherwise stall the sweep forever; a GPU hang ends itself through TDR.
per_file_timeout="${GBV_FILE_TIMEOUT:-2700}"

for f in $files; do
	rm -f bgpu.log
	marker="$(mktemp)"
	start=$(date +%s)

	timeout "$per_file_timeout" ./bgl_tests.exe -# "[#$f]" --gpu-validation > "$out/$f.out" 2>&1
	rc=$?
	secs=$(( $(date +%s) - start ))

	[ -f bgpu.log ] && cp bgpu.log "$out/$f.bgpu.log"

	# rc 2: no case in the file runs on this backend (Metal-only files). rc 4: every case skipped
	# itself, e.g. ShaderCache_test under validation. Neither is a failure.
	if [ "$rc" -ne 0 ] && [ "$rc" -ne 2 ] && [ "$rc" -ne 4 ]; then
		echo "FAIL $f rc=$rc ${secs}s $(tail -3 "$out/$f.out" | tr '\n' ' ' | cut -c1-300)" >> "$out/events.log"
	fi

	if [ -f "$out/$f.bgpu.log" ]; then
		# Strip timestamps, thread ids and object addresses so one message repeated per draw is one event.
		grep -E "\[D3D12\]|GPU-BASED VALIDATION|\[error\]|\[critical\]" "$out/$f.bgpu.log" \
			| grep -v "This API cannot be called on a closed command list" \
			| sed -E 's/^\[[^]]*\] \[thread [0-9]+\] //; s/0x[0-9A-Fa-f]+/0x_/g; s/Command List: .*//' \
			| cut -c1-400 | sort -u \
			| sed "s/^/GBV $f /" >> "$out/events.log"
	fi

	for crash in $(find . -maxdepth 1 -name 'bgl_tests_crash_*.log' -newer "$marker" 2>/dev/null); do
		cp "$crash" "$out/"
		echo "CRASH $f $out/$(basename "$crash")" >> "$out/events.log"
	done

	for log in $(find . -maxdepth 1 -name '*.log' -newer "$marker" ! -name 'bgpu.log' ! -name '*_crash_*' 2>/dev/null); do
		grep -iE "error|critical|fatal" "$log" | head -5 | sed "s|^|LOG $f $log: |" >> "$out/events.log"
	done

	rm -f "$marker"
	echo "$f rc=$rc ${secs}s $(tail -1 "$out/$f.out" | cut -c1-120)" >> "$out/summary.txt"
done

echo "DONE" >> "$out/events.log"
