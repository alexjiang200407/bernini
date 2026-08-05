"""Report what the Git LFS object store holds, against the ceiling and the free tier.

The store is billed by the byte above a provider's free allowance, and nothing about a
`git push` tells you how close you are. This is the check to run before adding a large
asset, and what `--check` makes a CI job fail on.

    python scripts/lfs_usage.py            # what is in the bucket, and the headroom
    python scripts/lfs_usage.py --check    # exit 1 if the ceiling is already passed
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import util.lfs_store as store  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero if usage is already past the ceiling")
    args = parser.parse_args()

    try:
        backend = store.open_store()
        used, count = backend.usage()
    except store.StoreError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"store    {backend.description}")
    print(f"objects  {count}")
    print(f"stored   {store.human(used)}")

    limit = backend.ceiling.limit
    if limit is None:
        print("ceiling  none set -- uploads are unbounded. Set store.ceiling in .lfsstore.")
        return 0

    share = (used / limit * 100) if limit else 0
    print(f"ceiling  {store.human(limit)} ({share:.1f}% used, "
          f"{store.human(max(0, limit - used))} free)")

    if used > limit:
        print("error: the store is already past its ceiling; uploads will be refused.",
              file=sys.stderr)
        return 1 if args.check else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
