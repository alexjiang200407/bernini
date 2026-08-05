"""Encrypt a secret so config.json can hold it without the file itself being the secret.

config.json is machine-local and git-ignored, but it is also the file people paste into an
issue when a build breaks, and the one a backup or a folder sync picks up. Storing a live
key there in plaintext turns a diagnostic file into a credential. Everything here exists to
break that: what lands in config.json is bound to the current user account, so a copy of
the file on another machine, or in a bug report, decrypts to nothing.

Protected values are tagged with the scheme that produced them, so a config written on one
machine is at least legible on another rather than silently wrong:

    dpapi:<base64>      Windows, CryptProtectData under the current user
    keychain:<account>  macOS, the value lives in the login keychain
    plain:<text>        anywhere else -- no worse than an environment variable, and the
                        tag is what makes that visible rather than assumed

Nothing here is a substitute for rotating a key that has actually leaked.
"""

import base64
import ctypes
import getpass
import os
import subprocess
import sys

# Mixed into the DPAPI ciphertext. It lives in a committed source file, so it separates this
# blob from other DPAPI users rather than defending against local code.
ENTROPY = b"bernini-lfs-credentials-v1"

KEYCHAIN_SERVICE = "bernini-lfs"


class SecretError(Exception):
    """A value could not be protected or recovered."""


def scheme():
    """The protection this platform can actually provide."""
    if sys.platform == "win32":
        return "dpapi"
    if sys.platform == "darwin":
        return "keychain"
    return "plain"


# --- Windows ---------------------------------------------------------------

class _Blob(ctypes.Structure):
    _fields_ = [("cbData", ctypes.c_uint32), ("pbData", ctypes.POINTER(ctypes.c_char))]

    @classmethod
    def of(cls, data):
        buffer = ctypes.create_string_buffer(data, len(data))
        return cls(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_char)))

    def value(self):
        return ctypes.string_at(self.pbData, self.cbData)


def _dpapi(function, data):
    """Run CryptProtectData/CryptUnprotectData over one buffer."""
    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    source = _Blob.of(data)
    entropy = _Blob.of(ENTROPY)
    result = _Blob()

    # Both calls take the same argument list; only the direction differs.
    ok = getattr(crypt32, function)(
        ctypes.byref(source), None, ctypes.byref(entropy),
        None, None, 0, ctypes.byref(result))
    if not ok:
        raise SecretError(f"{function} failed (error {ctypes.get_last_error()})")

    try:
        return result.value()
    finally:
        kernel32.LocalFree(result.pbData)


# --- macOS -----------------------------------------------------------------

def _security(args, stdin=None):
    try:
        return subprocess.run(["security"] + args, capture_output=True, text=True, input=stdin)
    except OSError as exc:
        raise SecretError(f"cannot run `security`: {exc}") from exc


def _keychain_store(account, value):
    done = _security([
        "add-generic-password", "-U",
        "-s", KEYCHAIN_SERVICE, "-a", account, "-w", value,
        "-D", "application password",
        "-j", "Bernini Git LFS object store credentials",
    ])
    if done.returncode:
        raise SecretError(f"could not write to the login keychain: {done.stderr.strip()}")


def _keychain_read(account):
    done = _security(["find-generic-password", "-s", KEYCHAIN_SERVICE, "-a", account, "-w"])
    if done.returncode:
        raise SecretError(
            f"no keychain entry for '{account}' under service '{KEYCHAIN_SERVICE}'. "
            f"Re-run `just init` to store it again.")
    return done.stdout.strip()


# --- API -------------------------------------------------------------------

def protect(value, account=None):
    """Return a tagged, protected form of `value` that is safe to write to config.json."""
    if not value:
        return ""

    how = scheme()
    if how == "dpapi":
        blob = _dpapi("CryptProtectData", value.encode("utf-8"))
        return "dpapi:" + base64.b64encode(blob).decode("ascii")
    if how == "keychain":
        account = account or getpass.getuser()
        _keychain_store(account, value)
        return f"keychain:{account}"
    return "plain:" + value


def unprotect(stored):
    """Recover a value written by protect(). Empty string when there is nothing stored."""
    if not stored:
        return ""

    tag, sep, rest = stored.partition(":")
    if not sep:
        # A value written before this was tagged, or edited in by hand.
        return stored

    if tag == "dpapi":
        if sys.platform != "win32":
            raise SecretError("this credential was protected with Windows DPAPI and cannot be "
                              "read on this platform; re-run `just init` here.")
        try:
            blob = base64.b64decode(rest)
        except ValueError as exc:
            raise SecretError(f"malformed dpapi value: {exc}") from exc
        return _dpapi("CryptUnprotectData", blob).decode("utf-8")
    if tag == "keychain":
        if sys.platform != "darwin":
            raise SecretError("this credential lives in a macOS keychain and cannot be read on "
                              "this platform; re-run `just init` here.")
        return _keychain_read(rest)
    if tag == "plain":
        return rest
    raise SecretError(f"unknown protection scheme '{tag}'")


def describe(stored):
    """How a stored value is protected, for output that must not include the value."""
    if not stored:
        return "not set"
    tag = stored.partition(":")[0]
    return {
        "dpapi": "set (encrypted to this Windows account)",
        "keychain": "set (in the macOS login keychain)",
        "plain": "set (PLAINTEXT -- this file is the secret)",
    }.get(tag, "set")


def restrict(path):
    """Cut a file's permissions to its owner. Best effort: never fatal."""
    try:
        if sys.platform == "win32":
            user = os.environ.get("USERNAME")
            if not user:
                return
            subprocess.run(["icacls", path, "/inheritance:r", "/grant:r", f"{user}:F"],
                           capture_output=True, text=True)
        else:
            os.chmod(path, 0o600)
    except OSError:
        pass
