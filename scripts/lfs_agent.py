"""Git LFS custom transfer agent: moves objects to and from this project's own bucket.

git-lfs invokes this, one process per concurrent transfer, from inside `git checkout`,
`git lfs pull` and `git lfs push`. It speaks the line-delimited JSON protocol documented at
https://github.com/git-lfs/git-lfs/blob/main/docs/custom-transfers.md -- one message per
line on stdin, one reply per line on stdout.

Registered as a *standalone* agent, so git-lfs never calls a batch API: the `action` field
the protocol would normally carry is absent, and the object key is derived here instead.
See docs/lfs.md.

Nothing here writes to stdout except protocol replies. Diagnostics go to stderr, which
git-lfs surfaces under GIT_TRACE=1.
"""

import argparse
import json
import os
import sys
import tempfile
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import util.lfs_store as store  # noqa: E402

# Progress is reported at most this often, in bytes moved. Every message is a line of JSON
# git-lfs has to parse, and a 134 MB object would otherwise produce thousands of them.
PROGRESS_INTERVAL = 4 << 20


def reply(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def fail(oid, message):
    reply({"event": "complete", "oid": oid, "error": {"code": 2, "message": str(message)}})


def progress_reporter(oid, size):
    """Callback that turns byte counts into throttled `progress` events."""
    state = {"sent": 0, "since": 0}

    def report(count):
        state["sent"] += count
        state["since"] += count
        if state["since"] < PROGRESS_INTERVAL and state["sent"] < size:
            return
        reply({
            "event": "progress",
            "oid": oid,
            "bytesSoFar": state["sent"],
            "bytesSinceLast": state["since"],
        })
        state["since"] = 0

    return report


def handle_upload(backend, message):
    oid, path = message["oid"], message["path"]
    try:
        # Re-uploading an object nobody can overwrite is pure egress; LFS objects are
        # content-addressed, so one that is present is by definition the right one.
        if backend.exists(oid):
            reply({"event": "complete", "oid": oid})
            return
        backend.put(oid, path, progress_reporter(oid, message.get("size", 0)))
    except (store.CeilingExceeded, store.StoreError, OSError) as exc:
        fail(oid, exc)
        return
    reply({"event": "complete", "oid": oid})


def handle_download(backend, message):
    oid = message["oid"]
    handle, temp = tempfile.mkstemp(dir=store.tmp_dir(), prefix=f"{oid[:8]}-")
    os.close(handle)

    try:
        actual = backend.get(oid, temp, progress_reporter(oid, message.get("size", 0)))
        # A corrupt object must not reach the cache under a name that says it is fine.
        if actual != oid:
            raise store.StoreError(f"content hash is {actual}, expected {oid}")
    except (store.StoreError, OSError) as exc:
        os.unlink(temp)
        fail(oid, exc)
        return

    reply({"event": "complete", "oid": oid, "path": temp})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()

    backend = None
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            message = json.loads(line)
        except json.JSONDecodeError as exc:
            print(f"lfs-agent: unparseable message: {exc}", file=sys.stderr)
            return 1

        event = message.get("event")
        if event == "terminate":
            return 0

        # An agent that dies mid-transfer reaches git-lfs as a bare "EOF" with nothing to
        # act on, so anything unhandled is turned back into a protocol error first.
        try:
            if event == "init":
                backend = store.open_store()
                print(f"lfs-agent: {message.get('operation')} via {backend.description}",
                      file=sys.stderr)
                reply({})
            elif event == "upload":
                handle_upload(backend, message)
            elif event == "download":
                handle_download(backend, message)
            else:
                print(f"lfs-agent: ignoring unknown event '{event}'", file=sys.stderr)
        except Exception as exc:  # noqa: BLE001
            traceback.print_exc(file=sys.stderr)
            detail = f"{type(exc).__name__}: {exc}"
            if event == "init":
                reply({"error": {"code": 32, "message": detail}})
                return 1
            if message.get("oid"):
                fail(message["oid"], detail)
            else:
                reply({"error": {"code": 2, "message": detail}})

    return 0


if __name__ == "__main__":
    sys.exit(main())
