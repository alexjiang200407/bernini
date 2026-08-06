"""Where this clone's Git LFS objects actually live, and how to read and write them.

Shared by the transfer agent (scripts/lfs_agent.py), which git-lfs invokes per transfer,
and by the seeding tool (scripts/lfs_seed.py), which fills a fresh bucket from the local
object cache. Both address objects the same way, so a bucket seeded by one is readable by
the other.

Settings come from .lfsstore, which is committed and describes the project, and are
overridable per machine by environment variable. A bucket name is not a secret and belongs
in the repository; the key pair does not, and comes from the environment or from what
`just init` stored.

Reading and writing are separate privileges. With store.readUrl set the bucket serves
downloads publicly, so a clone needs no key to fetch assets and one is only provisioned by
whoever adds them -- see docs/lfs.md.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import urllib.parse

import util.cmake_tools as ct
import util.config as cfg
import util.s3 as s3
import util.secrets as secrets

DEFAULT_PREFIX = "lfs"

SETTINGS = {
    "endpoint": "BERNINI_LFS_ENDPOINT",
    "bucket": "BERNINI_LFS_BUCKET",
    "prefix": "BERNINI_LFS_PREFIX",
    "region": "BERNINI_LFS_REGION",
    "ceiling": "BERNINI_LFS_CEILING",
    "readUrl": "BERNINI_LFS_READ_URL",
}

CHUNK = 1 << 20

UNITS = {"B": 1, "KB": 1 << 10, "MB": 1 << 20, "GB": 1 << 30, "TB": 1 << 40}


class CeilingExceeded(Exception):
    """An upload would take the store past the configured size ceiling."""


def parse_size(text):
    """Bytes from a string like '5 GB', '512MB' or a bare byte count. None if empty."""
    text = (text or "").strip().upper().replace(" ", "")
    if not text or text == "NONE":
        return None

    for unit in sorted(UNITS, key=len, reverse=True):
        if text.endswith(unit):
            number = text[: -len(unit)]
            try:
                return int(float(number) * UNITS[unit])
            except ValueError:
                raise ValueError(f"cannot read '{text}' as a size")
    try:
        return int(text)
    except ValueError:
        raise ValueError(f"cannot read '{text}' as a size")


def human(size):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if size < 1024 or unit == "TB":
            return f"{size:.0f} B" if unit == "B" else f"{size:.2f} {unit}"
        size /= 1024
    return f"{size:.2f} TB"


class StoreError(Exception):
    """The store is unreachable, misconfigured, or refused a transfer."""


def object_key(oid, prefix=DEFAULT_PREFIX):
    return f"{prefix}/{oid[:2]}/{oid[2:4]}/{oid}"


def cache_path(oid):
    """Where git-lfs keeps this object locally, whether or not it is there."""
    return os.path.join(ct.REPO_ROOT, ".git", "lfs", "objects", oid[:2], oid[2:4], oid)


def tmp_dir():
    path = os.path.join(ct.REPO_ROOT, ".git", "lfs", "tmp")
    os.makedirs(path, exist_ok=True)
    return path


def _git_config(key, config_file=None):
    cmd = ["git", "config"]
    if config_file:
        cmd += ["--file", config_file]
    cmd += ["--get", key]
    try:
        done = subprocess.run(cmd, cwd=ct.REPO_ROOT, capture_output=True, text=True)
    except OSError:
        return ""
    return done.stdout.strip() if done.returncode == 0 else ""


def setting(name):
    """One store setting: the environment first, then .lfsstore.

    Deliberately not .lfsconfig: git-lfs warns about every key it does not recognise as
    safe there, on every command it runs.
    """
    env = os.environ.get(SETTINGS[name], "").strip()
    if env:
        return env

    store_file = os.path.join(ct.REPO_ROOT, ".lfsstore")
    if os.path.exists(store_file):
        return _git_config(f"store.{name}", store_file)
    return ""


def source_of(name):
    """Where `setting` took a value from, for a message that must send the reader there."""
    if os.environ.get(SETTINGS[name], "").strip():
        return SETTINGS[name]
    return f"store.{name} in .lfsstore"


def credentials():
    """The access key pair: environment first, then what `just init` stored.

    config.json is consulted because git-lfs runs the agent from inside `git checkout`,
    including from GUI clients and shell integrations that never load a shell profile --
    where an environment variable simply is not there. The stored secret is encrypted to
    the current user account (see util/secrets.py), so the file is not itself the key.
    """
    access = (os.environ.get("BERNINI_LFS_ACCESS_KEY_ID") or "").strip()
    secret = (os.environ.get("BERNINI_LFS_SECRET_ACCESS_KEY") or "").strip()

    # One source or the other, never half of each: an access key signed with the wrong
    # secret fails as an authentication error that names neither.
    if access or secret:
        return access, secret

    stored = cfg.load().get("lfs") or {}
    try:
        return (stored.get("accessKeyId") or "").strip(), \
            secrets.unprotect(stored.get("secretAccessKey") or "")
    except secrets.SecretError as exc:
        raise StoreError(f"cannot read the stored LFS credential: {exc}") from exc


class Ceiling:
    """The refusal point for uploads, shared by every store.

    A backstop against a runaway loop, not an accounting system: concurrent agents each
    hold their own count, so a burst can overshoot by one object per agent. See
    docs/lfs.md.
    """

    def __init__(self, limit):
        self.limit = limit
        self._used = None

    def check(self, store, size):
        if self.limit is None:
            return
        if self._used is None:
            self._used, _ = store.usage()
        if self._used + size > self.limit:
            raise CeilingExceeded(
                f"upload refused: {human(self._used)} stored + {human(size)} would pass the "
                f"{human(self.limit)} ceiling. Raise it ({source_of('ceiling')}), or clear "
                f"unreachable objects, once you have checked what is in the bucket."
            )

    def charge(self, size):
        if self._used is not None:
            self._used += size


class FileStore:
    """A directory used as the object store, for a shared drive or an offline test."""

    def __init__(self, root, prefix, ceiling):
        self.root = root
        self.prefix = prefix
        self.ceiling = ceiling
        self.description = f"file://{root}"

    def _path(self, oid):
        return os.path.join(self.root, *object_key(oid, self.prefix).split("/"))

    def usage(self):
        """(bytes, object count) currently in the store."""
        total, count = 0, 0
        for root, _, files in os.walk(os.path.join(self.root, self.prefix)):
            for name in files:
                total += os.path.getsize(os.path.join(root, name))
                count += 1
        return total, count

    def exists(self, oid):
        return os.path.exists(self._path(oid))

    def put(self, oid, path, on_progress=None):
        size = os.path.getsize(path)
        self.ceiling.check(self, size)
        target = self._path(oid)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(path, target)
        self.ceiling.charge(size)
        if on_progress:
            on_progress(size)

    def get(self, oid, path, on_progress=None):
        source = self._path(oid)
        if not os.path.exists(source):
            raise StoreError(f"object {oid} is not in {self.description}")
        shutil.copyfile(source, path)
        if on_progress:
            on_progress(os.path.getsize(path))
        return _sha256(path)


NO_CREDENTIALS = (
    "that needs a key for the LFS object store, and this clone has none. Run "
    "`just init --lfs-key`, or set BERNINI_LFS_ACCESS_KEY_ID and "
    "BERNINI_LFS_SECRET_ACCESS_KEY. See docs/lfs.md."
)


class S3Store:
    """An S3-compatible bucket -- Cloudflare R2 in this project's case.

    Reads and writes are separate privileges here. When store.readUrl names a public
    endpoint, downloads go there unsigned, so a fresh clone fetches every asset with no
    credentials at all; uploads are always signed, and a clone with no key raises rather
    than pretending it could push.
    """

    def __init__(self, client, prefix, ceiling, read_url=""):
        self._client = client
        self._read_url = (read_url or "").rstrip("/")
        self.prefix = prefix
        self.ceiling = ceiling
        self.description = f"{client.endpoint}/{client.bucket}" if client else self._read_url
        if client and self._read_url:
            self.description += f" (reads via {self._read_url})"

    def _signed(self, what):
        if not self._client:
            raise StoreError(f"{what}: {NO_CREDENTIALS}")
        return self._client

    def _public_url(self, oid):
        return f"{self._read_url}/{object_key(oid, self.prefix)}"

    def usage(self):
        """(bytes, object count) currently in the bucket, under this project's prefix.

        Signed even when reads are public: a public bucket serves objects by key but does
        not list them.
        """
        client = self._signed("reporting store usage")
        try:
            total, count = 0, 0
            for _, size in client.list_objects(self.prefix):
                total += size
                count += 1
            return total, count
        except s3.S3Error as exc:
            raise StoreError(str(exc)) from exc

    def exists(self, oid):
        try:
            if self._read_url:
                return s3.exists_public(self._public_url(oid))
            return self._signed("checking the store").exists(object_key(oid, self.prefix))
        except s3.S3Error as exc:
            raise StoreError(str(exc)) from exc

    def put(self, oid, path, on_progress=None):
        client = self._signed(f"uploading {oid[:12]}")
        size = os.path.getsize(path)
        self.ceiling.check(self, size)
        try:
            # The OID is the SHA-256 of the content, which is exactly the payload hash
            # SigV4 wants -- so a signed upload costs no extra pass over the file.
            client.put_file(object_key(oid, self.prefix), path, oid, on_progress)
        except s3.S3Error as exc:
            raise StoreError(str(exc)) from exc
        self.ceiling.charge(size)

    def get(self, oid, path, on_progress=None):
        if self._read_url:
            try:
                return s3.download(self._public_url(oid), path, on_progress)
            except s3.S3Error as exc:
                if not self._client:
                    raise StoreError(f"{exc}\nThe public read endpoint "
                                     f"({source_of('readUrl')}) is not serving this object.") \
                        from exc
                # A key is the fallback, but never silently: everyone without one is broken
                # while this still works.
                print(f"lfs: {self._read_url} refused {oid[:12]} "
                      f"({str(exc).splitlines()[0][:160]}); falling back to a signed read. "
                      f"Clones without a key cannot fetch.", file=sys.stderr)

        try:
            return self._signed(f"downloading {oid[:12]}").get_file(
                object_key(oid, self.prefix), path, on_progress)
        except s3.S3Error as exc:
            raise StoreError(str(exc)) from exc


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(CHUNK), b""):
            digest.update(block)
    return digest.hexdigest()


def open_store():
    """Build the configured store, or raise StoreError explaining what is missing."""
    endpoint = setting("endpoint")
    prefix = setting("prefix") or DEFAULT_PREFIX

    if not endpoint:
        raise StoreError(
            "no LFS object store is configured: set store.endpoint in .lfsstore, "
            "or BERNINI_LFS_ENDPOINT in the environment."
        )

    try:
        ceiling = Ceiling(parse_size(setting("ceiling")))
    except ValueError as exc:
        raise StoreError(f"{source_of('ceiling')}: {exc}") from exc

    if endpoint.startswith("file://"):
        parsed = urllib.parse.urlsplit(endpoint)
        root = urllib.parse.unquote(parsed.path)
        # file://C:/x parses the drive letter as the host; file:///C:/x leaves a leading slash.
        if parsed.netloc:
            root = f"{parsed.netloc}{root}"
        elif os.name == "nt":
            root = root.lstrip("/")
        return FileStore(os.path.abspath(root), prefix, ceiling)

    bucket = setting("bucket")
    read_url = setting("readUrl")
    access, secret = credentials()

    # No key is not an error when reads are public: that is the state a fresh clone is in,
    # and it can fetch every asset. Only an upload has anything to refuse.
    if not access or not secret:
        if read_url:
            return S3Store(None, prefix, ceiling, read_url)
        raise StoreError(
            "no credentials for the LFS object store, and no public read endpoint "
            f"({source_of('readUrl')}) to fall back on: set BERNINI_LFS_ACCESS_KEY_ID and "
            "BERNINI_LFS_SECRET_ACCESS_KEY in the environment, or run `just init`. "
            "See docs/lfs.md."
        )

    try:
        client = s3.Client(endpoint, bucket, access, secret, setting("region") or s3.DEFAULT_REGION)
    except ValueError as exc:
        raise StoreError(str(exc)) from exc
    return S3Store(client, prefix, ceiling, read_url)
