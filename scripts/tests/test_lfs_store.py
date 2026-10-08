"""lfs_store and the agent resolve everything from the repository git-lfs runs them in.

One agent serves the engine and every game that points its git config at it, so each repo's
.lfsstore, object cache and key must be its own -- a game handed the engine's store or key
would upload its private assets to the engine's public bucket.
"""

import json
import os
import shutil
import subprocess
import sys

import pytest

import util.lfs_store as store

AGENT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "lfs_agent.py")


def git(cwd, *args):
    done = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    assert done.returncode == 0, f"git {' '.join(args)}: {done.stderr}"
    return done.stdout.strip()


def make_repo(path, store_dir, prefix="lfs"):
    os.makedirs(path)
    git(path, "init", "-q", "-b", "main")
    git(path, "config", "user.email", "t@example.com")
    git(path, "config", "user.name", "t")
    url = "file:///" + str(store_dir).replace("\\", "/").lstrip("/")
    with open(os.path.join(path, ".lfsstore"), "w", encoding="utf-8") as fh:
        fh.write(f"[store]\n\tendpoint = {url}\n\tprefix = {prefix}\n")
    return path


@pytest.fixture
def in_repo(monkeypatch):
    def enter(path):
        monkeypatch.chdir(path)
        monkeypatch.setattr(store, "_repo", None)
        for name in (*store.SETTINGS.values(), "BERNINI_LFS_ACCESS_KEY_ID",
                     "BERNINI_LFS_SECRET_ACCESS_KEY"):
            monkeypatch.delenv(name, raising=False)
    return enter


def test_settings_and_cache_come_from_the_repo_it_runs_in(tmp_path, in_repo):
    game = make_repo(tmp_path / "game", tmp_path / "store", prefix="game")
    os.makedirs(game / "sub")
    in_repo(game / "sub")

    assert os.path.samefile(store.repo_root(), game)
    assert store.setting("prefix") == "game"
    assert store.cache_path("ab" * 32).startswith(os.path.join(str(game), ".git", "lfs", "objects"))


def test_a_worktree_shares_its_clones_object_cache(tmp_path, in_repo):
    clone = make_repo(tmp_path / "clone", tmp_path / "store")
    git(clone, "add", ".lfsstore")
    git(clone, "commit", "-qm", "store")
    git(clone, "worktree", "add", "-q", str(tmp_path / "wt"), "-b", "feat")
    in_repo(tmp_path / "wt")

    assert os.path.samefile(store.repo_root(), tmp_path / "wt")
    assert os.path.samefile(store.git_dir(), clone / ".git")


def test_the_key_is_the_repos_own_and_never_this_checkouts(tmp_path, in_repo):
    game = make_repo(tmp_path / "game", tmp_path / "store")
    in_repo(game)
    assert store.credentials() == ("", "")

    store.store_credentials("game-id", "plain:game-secret")
    assert store.credentials() == ("game-id", "game-secret")
    assert "git config" in store.stored_credentials()[2]


def test_the_engines_config_json_is_read_after_the_git_config(tmp_path, in_repo):
    engine = make_repo(tmp_path / "engine", tmp_path / "store")
    os.makedirs(engine / "scripts")
    with open(engine / "scripts" / "config.json", "w", encoding="utf-8") as fh:
        json.dump({"lfs": {"accessKeyId": "engine-id", "secretAccessKey": "plain:s"}}, fh)
    in_repo(engine)

    assert store.credentials() == ("engine-id", "s")
    store.store_credentials("newer", "plain:n")
    assert store.credentials() == ("newer", "n")


@pytest.mark.skipif(shutil.which("git-lfs") is None, reason="needs git-lfs")
def test_one_agent_moves_each_repos_objects_to_its_own_store(tmp_path):
    env = {k: v for k, v in os.environ.items() if not k.startswith("BERNINI_LFS_")}
    repos = {}
    for name in ("engine", "game"):
        path = make_repo(tmp_path / name, tmp_path / f"{name}-store", prefix=name)
        for key, value in (("lfs.url", "https://bernini.invalid/lfs"),
                           ("lfs.standalonetransferagent", "bernini"),
                           ("lfs.customtransfer.bernini.path", sys.executable),
                           ("lfs.customtransfer.bernini.args", f'"{AGENT}"')):
            git(path, "config", key, value)
        git(path, "lfs", "install", "--local")
        git(path, "lfs", "track", "*.bin")
        with open(path / f"{name}.bin", "wb") as fh:
            fh.write(name.encode() * 64)
        git(path, "add", ".")
        git(path, "commit", "-qm", name)
        done = subprocess.run(["git", "lfs", "push", "--all", str(path)], cwd=path, env=env,
                              capture_output=True, text=True)
        assert done.returncode == 0, done.stderr
        repos[name] = path

    for name in repos:
        ours = list((tmp_path / f"{name}-store" / name).rglob("*"))
        assert any(p.is_file() for p in ours), f"{name}'s object is not in its own store"
    assert not (tmp_path / "engine-store" / "game").exists()
    assert not (tmp_path / "game-store" / "engine").exists()

    # A worktree checks out through the shared cache; drop it to force a download.
    game = repos["game"]
    shutil.rmtree(game / ".git" / "lfs" / "objects")
    os.remove(game / "game.bin")
    done = subprocess.run(["git", "checkout", "--", "game.bin"], cwd=game, env=env,
                          capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    with open(game / "game.bin", "rb") as fh:
        assert fh.read() == b"game" * 64


def test_an_environment_override_applies_to_whichever_repo_runs(tmp_path, in_repo, monkeypatch):
    game = make_repo(tmp_path / "game", tmp_path / "store", prefix="game")
    in_repo(game)
    monkeypatch.setenv("BERNINI_LFS_PREFIX", "engine")
    # Documented, not refused: the overrides are per command, so docs/lfs.md says never to export them.
    assert store.setting("prefix") == "engine"
