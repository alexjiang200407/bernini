# Git LFS — where the assets live, and how they get there

Everything under `assets/` — textures, meshes, environment maps, the golden images the render
tests compare against — is binary that git would otherwise store whole per revision. It is kept in
Git LFS: the repository holds a 130-byte pointer, and the bytes live in an object store addressed
by the SHA-256 of their content.

That store is **not** GitHub. It is a Cloudflare R2 bucket this project owns, reached by a custom
transfer agent that ships in `scripts/`. There is no LFS server in front of it.

**This document is a map, not a mirror.** It captures the design choices and the non-obvious
contracts. The source at each linked path is the source of truth; when this doc disagrees, trust
the source, then fix this doc.

---

## Why not GitHub

GitHub bills LFS against a budget that covers both stored bytes and bytes served, and it counts
every object ever pushed — including objects no longer reachable from any ref, which a history
rewrite does not reclaim. This repository accumulated 402 MB of superseded `pmrem` revisions alone,
none of it referenced at `HEAD`. When the budget is exceeded the endpoint stops serving, and the
failure is not graceful: the smudge filter fails mid-checkout, so `git pull` leaves assets *deleted*
in the working tree.

R2 has no egress fees and a 10 GB free tier, so a repository whose texture set only grows does not
turn into a recurring bill, and a full clone costs nothing to serve.

---

## Design Choices

* **No LFS server.** git-lfs supports a *standalone transfer agent*: with
  `lfs.standalonetransferagent` set, it skips the batch API entirely and hands each transfer to a
  subprocess. So there is nothing to host, nothing to authenticate against, and nothing to keep
  running — the client talks S3 to the bucket directly. The cost is that every machine needs
  credentials of its own, which is the right trade for a project with few contributors and no
  anonymous readers.

* **The agent is stdlib-only.** [scripts/lfs_agent.py](../scripts/lfs_agent.py) signs its own SigV4
  requests via [scripts/util/s3.py](../scripts/util/s3.py) rather than taking a dependency on boto3.
  git-lfs invokes the agent from inside `git checkout`, on any machine that clones the repository —
  including ones that never run the build scripts — so it has to work with nothing installed beyond
  a Python interpreter. `scripts/requirements.txt` is a registry of tool binaries, not libraries,
  and this keeps it that way.

* **The OID is the payload hash.** SigV4 wants a SHA-256 of the request body in
  `x-amz-content-sha256`, and an LFS object's OID *is* the SHA-256 of its content. Signed uploads
  therefore cost no extra pass over the file.

* **Settings are committed; credentials are not.** [.lfsstore](../.lfsstore) carries the endpoint,
  bucket, prefix and region — a bucket name is not a secret and every clone must agree on it. The
  key pair never is.

* **The credential is stored, but encrypted to the user account.** An environment variable is not
  enough on its own: git-lfs runs the agent from inside `git checkout`, which also happens from a
  GUI client, an IDE's git integration or a shell extension — none of which load a shell profile,
  so the variable is simply absent and the checkout silently writes pointer text. So `just init`
  records the key in `scripts/config.json`, which every script already reads. That file is
  git-ignored, but it is also the one people paste into a bug report, so the secret is passed
  through [scripts/util/secrets.py](../scripts/util/secrets.py) first: DPAPI on Windows, the login
  keychain on macOS. What lands on disk is then bound to the current user account and decrypts to
  nothing anywhere else. **Anywhere else — Linux — there is no key store and the value is written
  in plaintext**, tagged `plain:` and warned about at the terminal, so on those machines
  `config.json` *is* the credential and the environment is the better route. Environment variables
  win when set either way, which is how CI supplies them.

* **Three files, because git-lfs forces the split.** The store's location cannot go in
  `.lfsconfig`: git-lfs warns about every key it does not recognise as safe there, on every command
  it runs, so those settings live in `.lfsstore`, which only
  [scripts/util/lfs_store.py](../scripts/util/lfs_store.py) reads. The two keys that make git-lfs *use*
  the agent — `lfs.standalonetransferagent` and `lfs.customtransfer.*` — are refused from
  `.lfsconfig` outright, since a file that arrives with a clone must not get to name the program git
  executes; `just init` writes those into the clone's local git config. `.lfsconfig` is left holding
  only `lfs.url`, which git-lfs insists on having before it will consider an agent at all.

* **The agent's configured paths are absolute.** git-lfs runs it from whatever directory the
  triggering git command was in, so a relative path does not survive — which is the other reason
  `just init` has to write them per machine.

* **The size ceiling is a client-side backstop, because no provider-side one exists.**
  Cloudflare has no spending cap: exceed the free tier and it bills, with nothing to stop it.
  Removing the LFS server is what makes a client-side limit worth anything — every byte written
  to the bucket passes through [scripts/lfs_agent.py](../scripts/lfs_agent.py) or
  [scripts/lfs_seed.py](../scripts/lfs_seed.py), so `store.ceiling` in `.lfsstore` is enforceable
  in a way it could not be if a server accepted writes from anywhere. It guards against a
  runaway loop, not against a determined `aws s3 cp`.

* **Objects are migrated by reachability, not by copying a bucket.**
  [scripts/lfs_seed.py](../scripts/lfs_seed.py) lists what the refs actually reference and uploads it
  from `.git/lfs/objects`. Dead revisions of large assets stay behind rather than being paid for
  again, and the migration needs no cooperation from the previous host — one clone that already has
  the objects on disk is enough.

---

## Setting up a machine

1. Create an R2 API token scoped to the bucket. **Object Read & Write is enough** — an admin token
   would also be able to create and delete buckets, which nothing here needs.

2. `just init`. It installs the git-lfs filters, points the clone at the store, asks for the key
   pair (the secret is not echoed), and fetches anything still sitting as a pointer. The secret is
   encrypted before it is written, and `scripts/config.json` is restricted to your account.

To supply the credentials some other way — CI, or a machine where you would rather not store them —
set `BERNINI_LFS_ACCESS_KEY_ID` and `BERNINI_LFS_SECRET_ACCESS_KEY` in the environment instead.
They take precedence over anything stored, and `just init` will not ask.

Rotating a key is `just init --lfs-key`, which asks again and replaces what is stored. A plain
`just init` will not: it carries an existing credential across a re-run untouched, so nothing else
you re-run can quietly drop it.

The pre-commit hook refuses to commit `scripts/config.json`, which is git-ignored anyway, so it
takes `git add -f` to get it near a commit and the hook stops that too.

`just run` and `just test` refuse to launch a binary while any asset is still pointer text
([scripts/util/lfs.py](../scripts/util/lfs.py)), because the binaries otherwise fail on a "corrupt"
asset that is really ASCII. If you see that error after `just init`, the credentials are the usual
cause.

## Seeding or re-seeding the bucket

```bash
just lfs-seed --dry-run    # what would be uploaded, and what this clone is missing
just lfs-seed              # upload everything the bucket does not already hold
just lfs-seed --head-only  # only what HEAD references, ignoring history
```

Uploads are skipped for objects already in the bucket, so re-running is cheap and safe.

## Staying inside the free tier

R2 gives 10 GB of storage, 1M Class A (write) and 10M Class B (read) operations a month, and
egress that is unmetered and free. Every ref of this repository together references ~742 MB across
71 objects, a full clone costs ~27 reads, and a complete re-seed costs ~71 writes — so the binding
constraint is storage, at roughly 7% of the allowance.

There is no cap on Cloudflare's side, so `store.ceiling` in `.lfsstore` is the enforcement:

```bash
just lfs-usage           # objects, bytes stored, and headroom to the ceiling
just lfs-usage --check   # exit 1 once the ceiling is passed, for a CI job
```

Uploads past the ceiling are refused by both the agent and the seeder; `just lfs-seed` stops at
the first refusal rather than failing every remaining object in turn. Downloads are never charged
against it — a ceiling that could block a checkout would break clones for everyone the moment the
bucket filled.

Two limits worth knowing. Concurrent agents each hold their own byte count, so a burst can
overshoot by up to one object per agent, which is why the ceiling is set at half the free tier
rather than just under it. And it binds only code that goes through these scripts: anything writing
to the bucket with another S3 client is unguarded.

## Overrides

Every setting in `.lfsstore` is overridable per machine by environment variable —
`BERNINI_LFS_ENDPOINT`, `BERNINI_LFS_BUCKET`, `BERNINI_LFS_PREFIX`, `BERNINI_LFS_REGION`,
`BERNINI_LFS_CEILING`. An
endpoint of the form `file:///path/to/dir` uses a directory as the object store instead of a
bucket, which is how the agent is exercised without credentials and what a shared network drive
would use.

## CI

The build jobs in [.github/workflows/ci.yml](../.github/workflows/ci.yml) check out with `lfs: false`:
they compile and none of them runs a suite, so `assets/` is only staged, never read. A job that
runs a test suite needs the assets, and therefore needs `BERNINI_LFS_*` secrets in the environment
plus a `just init` to point the clone at the store. Neither `actions/checkout`'s `lfs: true` nor a
bare `git lfs pull` works: both go to GitHub's endpoint, which holds nothing.
