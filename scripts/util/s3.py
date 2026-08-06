"""Minimal SigV4-signed GET/PUT/HEAD against an S3-compatible endpoint.

Exists so the Git LFS transfer agent stays stdlib-only, for the reasons in docs/lfs.md.

Only what an LFS object store needs is implemented: no multipart, no ACLs. Objects are
whole-file PUTs of at most a few hundred megabytes, which is inside the 5 GiB single-PUT
limit on both S3 and R2.
"""

import hashlib
import hmac
import os
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from xml.etree import ElementTree

# Cloudflare R2 ignores the region but still requires it in the credential scope.
DEFAULT_REGION = "auto"

CHUNK = 1 << 20

USER_AGENT = "bernini-lfs/1"

# SHA-256 of the empty string: the payload hash of every request that sends no body.
EMPTY_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


class S3Error(Exception):
    """A request reached the endpoint and came back as a failure."""


def _fetch(request, timeout):
    # Cloudflare's public r2.dev endpoint answers urllib's default User-Agent with a 403.
    if not request.has_header("User-agent"):
        request.add_header("User-Agent", USER_AGENT)
    try:
        return urllib.request.urlopen(request, timeout=timeout)
    except urllib.error.HTTPError as exc:
        body = exc.read(2048).decode("utf-8", "replace").strip()
        raise S3Error(f"{exc.code} {exc.reason}: {body}") from exc
    except urllib.error.URLError as exc:
        host = urllib.parse.urlsplit(request.full_url).netloc
        raise S3Error(f"cannot reach {host}: {exc.reason}") from exc


def _stream(response, path, on_progress):
    """Write a response body to a path, returning the SHA-256 of what was written."""
    digest = hashlib.sha256()
    with open(path, "wb") as out:
        while True:
            block = response.read(CHUNK)
            if not block:
                break
            digest.update(block)
            out.write(block)
            if on_progress:
                on_progress(len(block))
    return digest.hexdigest()


def download(url, path, on_progress=None, timeout=600):
    """GET a URL that needs no credentials, returning the SHA-256 of what was written.

    The read half of a public bucket: an object is addressed by the same key a signed
    request would use, so nothing about the layout changes when a clone has no key.
    """
    with _fetch(urllib.request.Request(url, method="GET"), timeout) as response:
        return _stream(response, path, on_progress)


def exists_public(url, timeout=30):
    """Whether a URL that needs no credentials resolves to an object."""
    try:
        with _fetch(urllib.request.Request(url, method="HEAD"), timeout):
            return True
    except S3Error as exc:
        if str(exc).startswith("404 "):
            return False
        raise


class _ProgressReader:
    """File wrapper that reports bytes as urllib pulls them off the upload body."""

    def __init__(self, fh, on_progress):
        self._fh = fh
        self._on_progress = on_progress

    def read(self, size=-1):
        data = self._fh.read(size)
        if data and self._on_progress:
            self._on_progress(len(data))
        return data


def _quote(key):
    """Percent-encode an object key, leaving the separators between segments."""
    return urllib.parse.quote(key, safe="/~")


def _sign(key, msg):
    return hmac.new(key, msg.encode("utf-8"), hashlib.sha256).digest()


def _signing_key(secret, datestamp, region, service):
    key = _sign(f"AWS4{secret}".encode("utf-8"), datestamp)
    key = _sign(key, region)
    key = _sign(key, service)
    return _sign(key, "aws4_request")


class Client:
    """A signed connection to one bucket on one S3-compatible endpoint."""

    def __init__(self, endpoint, bucket, access_key, secret_key, region=DEFAULT_REGION):
        if not endpoint or not bucket:
            raise ValueError("an S3 endpoint and bucket are both required")
        if not access_key or not secret_key:
            raise ValueError("an S3 access key and secret key are both required")

        self.endpoint = endpoint.rstrip("/")
        self.bucket = bucket
        self.region = region or DEFAULT_REGION
        self._access_key = access_key
        self._secret_key = secret_key

        parsed = urllib.parse.urlsplit(self.endpoint)
        if parsed.scheme not in ("http", "https") or not parsed.netloc:
            raise ValueError(f"endpoint must be an http(s) URL, got '{endpoint}'")
        self._host = parsed.netloc
        self._scheme = parsed.scheme
        self._base_path = parsed.path.rstrip("/")

    def url_for(self, key):
        return f"{self.endpoint}/{self.bucket}/{_quote(key)}"

    def _key_uri(self, key):
        return f"{self._base_path}/{self.bucket}/{_quote(key)}"

    def _authorize(self, method, canonical_uri, payload_sha256,
                   extra_headers=None, canonical_query=""):
        """Build the signed header set for one request."""
        now = datetime.now(timezone.utc)
        amz_date = now.strftime("%Y%m%dT%H%M%SZ")
        datestamp = now.strftime("%Y%m%d")

        headers = {
            "host": self._host,
            "x-amz-content-sha256": payload_sha256,
            "x-amz-date": amz_date,
        }
        headers.update({k.lower(): v for k, v in (extra_headers or {}).items()})

        signed_names = ";".join(sorted(headers))
        canonical_headers = "".join(f"{k}:{headers[k]}\n" for k in sorted(headers))

        canonical_request = "\n".join([
            method,
            canonical_uri,
            canonical_query,
            canonical_headers,
            signed_names,
            payload_sha256,
        ])

        scope = f"{datestamp}/{self.region}/s3/aws4_request"
        string_to_sign = "\n".join([
            "AWS4-HMAC-SHA256",
            amz_date,
            scope,
            hashlib.sha256(canonical_request.encode("utf-8")).hexdigest(),
        ])

        signature = hmac.new(
            _signing_key(self._secret_key, datestamp, self.region, "s3"),
            string_to_sign.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()

        headers["Authorization"] = (
            f"AWS4-HMAC-SHA256 Credential={self._access_key}/{scope}, "
            f"SignedHeaders={signed_names}, Signature={signature}"
        )
        return headers

    def _open(self, request, timeout):
        return _fetch(request, timeout)

    def exists(self, key, timeout=30):
        """Whether the object is already in the bucket."""
        headers = self._authorize("HEAD", self._key_uri(key), EMPTY_SHA256)
        request = urllib.request.Request(self.url_for(key), method="HEAD", headers=headers)
        try:
            with self._open(request, timeout):
                return True
        except S3Error as exc:
            if str(exc).startswith("404 "):
                return False
            raise

    def put_file(self, key, path, sha256, on_progress=None, timeout=600):
        """Upload a file whose SHA-256 is already known, streaming it from disk."""
        size = os.path.getsize(path)
        headers = self._authorize(
            "PUT", self._key_uri(key), sha256, {"content-length": str(size)}
        )
        headers["Content-Length"] = str(size)

        with open(path, "rb") as fh:
            body = _ProgressReader(fh, on_progress) if on_progress else fh
            request = urllib.request.Request(
                self.url_for(key), data=body, method="PUT", headers=headers
            )
            with self._open(request, timeout):
                pass

    def list_objects(self, prefix="", timeout=60):
        """Yield (key, size) for every object under a prefix, following pagination.

        One Class A operation per page of 1000, which is what makes a usage check cheap
        enough to run before every upload.
        """
        token = None
        while True:
            params = [("list-type", "2"), ("max-keys", "1000")]
            if prefix:
                params.append(("prefix", prefix))
            if token:
                params.append(("continuation-token", token))

            # SigV4 wants the query sorted by name and each part RFC3986-encoded.
            canonical_query = "&".join(
                f"{urllib.parse.quote(k, safe='-_.~')}={urllib.parse.quote(v, safe='-_.~')}"
                for k, v in sorted(params)
            )
            canonical_uri = f"{self._base_path}/{self.bucket}/"
            headers = self._authorize("GET", canonical_uri, EMPTY_SHA256,
                                      canonical_query=canonical_query)

            url = f"{self.endpoint}/{self.bucket}/?{canonical_query}"
            request = urllib.request.Request(url, method="GET", headers=headers)
            with self._open(request, timeout) as response:
                body = response.read()

            root = ElementTree.fromstring(body)
            namespace = {"s3": root.tag[1:root.tag.index("}")]} if root.tag.startswith("{") else {}
            path = "s3:Contents" if namespace else "Contents"
            for entry in root.findall(path, namespace):
                key = entry.find("s3:Key" if namespace else "Key", namespace)
                size = entry.find("s3:Size" if namespace else "Size", namespace)
                if key is not None and size is not None:
                    yield key.text, int(size.text)

            truncated = root.find("s3:IsTruncated" if namespace else "IsTruncated", namespace)
            if truncated is None or truncated.text != "true":
                return
            next_token = root.find(
                "s3:NextContinuationToken" if namespace else "NextContinuationToken", namespace)
            if next_token is None:
                return
            token = next_token.text

    def get_file(self, key, path, on_progress=None, timeout=600):
        """Download an object to a path, returning its SHA-256 as written."""
        headers = self._authorize("GET", self._key_uri(key), EMPTY_SHA256)
        request = urllib.request.Request(self.url_for(key), method="GET", headers=headers)
        with self._open(request, timeout) as response:
            return _stream(response, path, on_progress)
