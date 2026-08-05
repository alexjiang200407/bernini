"""Fill the LFS object store from this clone's local cache.

The migration tool. Every object any ref still references is listed, matched against
.git/lfs/objects, and uploaded to the configured bucket -- so moving off a previous LFS
host needs no cooperation from it, only one clone that already has the objects on disk.

Objects reachable from no ref are skipped, which is the point of migrating by reachability
rather than by copying a bucket: dead revisions of large assets stay behind instead of
being paid for again.

    python scripts/lfs_seed.py --dry-run     # what would be uploaded, and what is missing
    python scripts/lfs_seed.py               # upload everything missing from the bucket
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import util.cmake_tools as ct  # noqa: E402
import util.lfs_store as store  # noqa: E402


def referenced_objects(all_refs):
    """(oid, path) for every LFS object reachable from the requested refs."""
    cmd = ["git", "lfs", "ls-files", "-l"]
    if all_refs:
        cmd.append("--all")

    try:
        done = subprocess.run(cmd, cwd=ct.REPO_ROOT, capture_output=True, text=True)
    except OSError as exc:
        raise SystemExit(f"error: cannot run git lfs: {exc}")
    if done.returncode:
        raise SystemExit(f"error: {' '.join(cmd)} failed:\n{done.stderr.strip()}")

    seen = {}
    for line in done.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        oid, path = parts[0], parts[2]
        seen.setdefault(oid, path)
    return sorted(seen.items(), key=lambda item: item[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true",
                        help="list what would be uploaded without uploading it")
    parser.add_argument("--head-only", action="store_true",
                        help="only objects reachable from HEAD, rather than from every ref")
    parser.add_argument("--force", action="store_true",
                        help="upload even objects the bucket already holds")
    args = parser.parse_args()

    try:
        backend = store.open_store()
    except store.StoreError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    objects = referenced_objects(all_refs=not args.head_only)
    if not objects:
        print("no LFS objects are referenced; nothing to do.")
        return 0

    scope = "HEAD" if args.head_only else "every ref"
    print(f"{len(objects)} object(s) referenced from {scope}; store is {backend.description}")

    missing, pending, total = [], [], 0
    for oid, path in objects:
        local = store.cache_path(oid)
        if not os.path.exists(local):
            missing.append((oid, path))
            continue
        pending.append((oid, path, local, os.path.getsize(local)))
        total += os.path.getsize(local)

    for oid, path in missing:
        print(f"  missing from the local cache: {path} ({oid[:12]})", file=sys.stderr)
    if missing:
        print(f"warning: {len(missing)} object(s) are not on this machine and cannot be "
              f"uploaded from it.", file=sys.stderr)

    if args.dry_run:
        print(f"would consider {len(pending)} object(s), {store.human(total)} before skipping "
              f"what the bucket already holds.")
        return 1 if missing else 0

    uploaded, skipped, sent, failed = 0, 0, 0, 0
    for index, (oid, path, local, size) in enumerate(pending, start=1):
        prefix = f"[{index}/{len(pending)}]"
        try:
            if not args.force and backend.exists(oid):
                skipped += 1
                continue
            print(f"{prefix} {path} ({store.human(size)})")
            backend.put(oid, local)
            uploaded += 1
            sent += size
        except store.CeilingExceeded as exc:
            # Stop rather than continue: every remaining object faces the same limit, and
            # grinding through them just turns one refusal into hundreds.
            print(f"{prefix} {exc}", file=sys.stderr)
            failed += 1
            break
        except (store.StoreError, OSError) as exc:
            print(f"{prefix} error: {path}: {exc}", file=sys.stderr)
            failed += 1

    print(f"uploaded {uploaded} object(s), {store.human(sent)}; "
          f"{skipped} already present; {failed} failed.")
    return 1 if (failed or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
