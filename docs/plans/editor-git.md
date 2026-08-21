# Version control in the editor

## Context

The editor authors a loose `Data/` tree precisely because separate files are "the unit version
control wants" ([Project.h](../../apps/editor/src/Project/Project.h)), yet nothing in the editor
touches version control. Saving or sharing work means leaving for a terminal, and the artists this
is for will not. Git is the backend today and may not be forever.

Version control applies to that loose tree and to nothing else. `.bpak` is the ship format, packed
from the tree for a released game and never read back by the editor
([docs/archives.md](../archives.md)), so an archive is a build output — nothing here ever versions
one, and no decision below has to account for them.

The request began as "is there a Qt git panel we can drop in". There is not — see § What the survey
found — so this builds one, in the editor's own vocabulary.

## Decisions

**ADR-1 — drive the `git` command line through `QProcess`.** *Rejected:* `libgit2`, which vcpkg
already carries. It has no LFS filter, so a commit made through it would store raw binary where a
pointer belongs and a checkout would leave pointer text in place of a mesh. The command line gets
`.gitattributes`, the credential helper and this project's standalone R2 transfer agent
([docs/lfs.md](../lfs.md)) for free. Linking libgit2 would also compile a parser of untrusted
repository data into the editor and make its CVEs ours to ship — CVE-2024-24577 is heap corruption
in `git_index_add`, the call a stage-these-assets button makes — where the command line is patched
by the OS. *Also rejected:* an existing Qt panel — none exists.

**ADR-2 — the interface never names git.** The menu is *Version Control* and the verbs are *Get
Latest*, *Submit*, *Revert*, *History* and *Undo*; every message shown is written in those words and
no git output reaches the user verbatim. *Rejected:* git's own vocabulary — it names a backend that
may be replaced, and it is vocabulary the target user does not have. The Perforce family of words
was taken instead because artists in games already have it.

**ADR-3 — the UI talks to an interface, not to a process.** `IVersionControl` carries the verbs;
`GitVersionControl` is the only implementation and no git type crosses the seam. *Rejected:*
calling `QProcess` from the widget — a backend swap would then be a UI rewrite, and the seam is what
makes any of this testable at all
([apps/editor/CLAUDE.md](../../apps/editor/CLAUDE.md) § What is testable).

**ADR-4 — Submit is one action.** It records the change and publishes it together. *Rejected:*
exposing stage, commit and push separately — three steps with no meaning to the user, and a commit
that is never pushed is work that looks saved and is not.

**ADR-5 — refuse rather than strand.** Anything that would leave the working tree in a state the
user cannot get out of is blocked before it starts, naming the assets involved. *Rejected:* letting
git's conflicted state happen — it strands exactly the user this exists for.

**ADR-6 — an existing repository only.** The menu is disabled unless the project already sits inside
one. *Rejected:* creating or cloning repositories from the editor, which would put LFS setup and
remote authentication — the two things that actually go wrong — inside the editor.

**ADR-7 — the child process is pinned and every path is fenced.** `git` is resolved once to an
absolute path through `QStandardPaths::findExecutable` and never invoked by name; `--` precedes
every path list; a path that does not resolve under the repository root is refused; and
`GIT_TERMINAL_PROMPT=0` is set so a repository whose credentials are missing fails instead of
blocking on a prompt nobody can see. *Rejected:* invoking `"git"` and letting the OS resolve it —
on Windows `CreateProcess` searches the working directory before `PATH` (CVE-2022-25255), and the
working directory here is the project root, whose contents arrived with the assets. *Rejected:*
trusting the repository's own config — `core.pager`, `diff.external`, `core.hooksPath` and
`filter.*` all execute commands, and one of those filters is git-lfs, so they cannot be disabled;
a hostile repository is equivalent to a hostile executable, and ADR-6 puts a human at the clone.

**ADR-8 — Get Latest fast-forwards or refuses.** Never a merge and never a rebase: history stays a
single line, so there is no conflicted state to be stranded in and no merge commit for a user with
no mental model of one to interpret. *Rejected:* merging, which is what produces the conflicted tree
ADR-5 exists to avoid; and rebasing, which rewrites work already submitted and would need a force
push to publish.

**ADR-9 — Undo is a new submission, not a rewrite.** Undoing restores the assets an earlier
submission touched by recording that restoration as the newest submission; the undone one stays in
the history. *Rejected:* resetting or otherwise rewriting — it diverges the local line from the
remote, which under ADR-8 can then never be published again, and it destroys the record of what
happened, which is most of what a history is for.

**ADR-10 — an update that would break the project is refused, on the same rule a deletion already
is.** Before Get Latest, Revert or Undo touches anything, two checks: the tree the update would
produce must leave no reference dangling — asked of `assetlib::AssetRefGraph`, the same graph the
Content Explorer's delete consults for its `blockers` — and no file it would change may be open in
an editor window. *Rejected:* a new locking or invalidation system. The editor has no "an asset
changed on disk" path at all today (`Project::ReloadStore` is called only from `Create` and `Open`),
so building one is its own feature; refusing is what ADR-5 does everywhere else, and it costs two
checks rather than a subsystem.

## Non-goals

- **Branches.** Switching one rewrites `Data/` under a live editor; every open viewport and asset
  handle would need invalidating.
- **Merging and rebasing, and any rewriting of what is already submitted.** ADR-8 and ADR-9.
- **Restoring one asset from an old submission.** History undoes a whole submission; picking a
  single asset out of one needs a preview to be worth anything, and previews are their own feature.
- **Conflict resolution of any kind**, including "keep mine / take theirs". ADR-5 refuses instead.
- **LFS.** Neither setting it up nor warning that a binary asset is not covered by it.
- **Credentials, remotes, authentication.** Whoever cloned the project configured them.
- **A general "an asset changed on disk" invalidation.** ADR-10 refuses the update instead, and a
  live-reload path for open assets is its own feature with its own reasons to exist.
- **A second `IVersionControl` implementation.** The seam exists so one is possible, not so one is
  written now.

## Acceptance

`just test editor`, new `[vcs]` tag. The logic is free functions and one interface over a
repository path, driven against real repositories the tests build with the git command line in a
temporary directory:

- change listing covers added, modified, deleted, renamed and untracked, and paths containing
  spaces and non-ASCII bytes;
- Submit refuses when the remote has moved, and says so without naming git;
- Get Latest fast-forwards when the local line has not moved, and refuses without touching the
  working tree when it has;
- Revert restores a modified asset;
- History lists submissions newest first with the assets each touched;
- Undo records a new submission restoring those assets, leaves the undone one still listed, and
  refuses when it would conflict;
- an update that would delete an asset another still references is refused, naming both, and the
  working tree is unchanged;
- an update that would change a file open in an editor window is refused, naming it;
- a project outside a repository leaves the menu disabled.

The Qt wiring stays uncovered, as every other window does; the dialogs are driven directly rather
than through `exec()`.

## What the survey found

**No reusable Qt git panel exists.** What turned up falls in three buckets, none usable: standalone
GPL applications (QGit, GitBusyLivin, CuteGit) which are whole apps rather than widgets; IDE plugins
(Qt Creator's `VcsBase`, KDevelop's) coupled to their host's plugin system; and libgit2 wrappers —
[libqgit2](https://github.com/KDE/libqgit2) is plumbing rather than UI, is LGPL, is not in vcpkg,
and its own versioning ties it to libgit2 0.22 against a current 1.9.

**libgit2 cannot carry LFS.** It has a filter API but no LFS filter, and git-lfs's filter is a
subprocess. This decides ADR-1 on its own.

**A project need not use LFS at all.** `bernini-test-project` keeps its assets in git directly — its
`.gitattributes` sets `filter=lfs` and then unsets it again with `!filter`, so `git lfs ls-files`
reports nothing — and for a project that small, hosted on GitHub, that is the intended arrangement.
So the panel must be correct on a repository with no LFS in play, and ADR-1's LFS reasoning rests on
the engine repository ([docs/lfs.md](../lfs.md)) and on the projects that will use it, not on the
test project.

**The editor has no subprocess helper and `core` has none either** — nothing in `libs/core` spawns a
process, and `QProcess` could not live there anyway, since Qt is editor-only
([apps/editor/CLAUDE.md](../../apps/editor/CLAUDE.md) § Rules).

**What the editor already has that this uses.** `background::RunWithLoadingScreen`
([Async/BackgroundTask.h](../../apps/editor/src/Async/BackgroundTask.h)) runs work off the UI thread
behind a modal screen, and is testable by arming `editor::test::OnLoadingScreen` first — which is
what keeps a slow publish from freezing the editor and still leaves it drivable. `MainWindow::Build`
adds top-level menus in code (`menuBar()->addMenu("Render")`), which is what the Version Control menu
follows; `AssetImporterDialog` is the pattern for a dialog the menu opens.
`Project::GetProjectFile` gives the path the repository is discovered from.

**Constraints from the tree.** `apps/editor` uses one flat `editor::` namespace, so every name must
carry its own qualifier. A header may forward-declare only within its own namespace. Tests must set
the commit identity per invocation: a runner with no `user.email` cannot commit.

## What changes

| Where | What |
|---|---|
| `apps/editor/src/VersionControl/` | new: the process runner, the seam, the git implementation |
| `apps/editor/src/Windows/VersionControl/` | new: the two dialogs the menu opens |
| `apps/editor/qt/Windows/VersionControl/` | new: `PendingChangesDialog.ui`, `HistoryDialog.ui` |
| `apps/editor/src/MainWindow.cpp` | the Version Control menu, built as the Render menu is |
| `apps/editor/tests/src/` | new `[vcs]` cases |
| `docs/` | a page for the subsystem, written when task 6 lands |

**What could break.** Nothing existing calls into this, so the risk is outward: a `QProcess` started
on a worker thread must be created on that thread; a Submit that publishes gigabytes of LFS objects
must not block the UI thread; and a git command run in a repository the developer also has open in
a terminal must not take a lock it does not need — every read is a plumbing command, and only
Submit, Get Latest, Revert and Undo write.

## Tasks

**1 — the process runner and repository discovery.** `VersionControl/git_cli.{h,cpp}`: `RunGit(directory,
arguments)` returning exit code, stdout and stderr, synchronous and safe to call from a worker; and
`FindRepositoryRoot(path)` returning `std::optional<std::filesystem::path>`. No shell, so no
quoting. *Gate:* `[vcs]` — the root found from a nested subdirectory; `nullopt` outside a
repository; a failing command's stderr surfacing; an argument containing a space arriving intact; a
path named `-o` reaching git as a path and not an option; a path outside the repository root
refused; output larger than a pipe buffer read without deadlocking; a command that would prompt for
credentials failing rather than blocking.

**2 — the seam and the change list.** `VersionControl/IVersionControl.h` with `ListChanges()` and the
`PendingChange` / `ChangeKind` PODs; `VersionControl/GitVersionControl.{h,cpp}` implementing it over
`git status --porcelain=v2 -z`. *Gate:* `[vcs]` — each of added, modified, deleted, renamed and
untracked mapping to one `ChangeKind`; a path with a space and one with non-ASCII bytes surviving;
a clean repository listing nothing; an ignored file absent.

**3 — Submit, Get Latest, Revert, and their refusals.** The three verbs on `IVersionControl`, each
returning a `VersionControlOutcome` — a status and the assets it is about, never a message, so the
UI writes the words; Get
Latest is fast-forward only (ADR-8). *Gate:* `[vcs]`, over two clones of one temporary repository — Submit refuses after
the other clone has published, and the reason names assets rather than git; Get Latest refuses when
the local line has moved and leaves the tree byte-identical with no merge in progress; Get Latest
fast-forwards when it has not; Revert restores; Submit refuses when the commit identity is unset.

**4 — the reference guard.** ADR-10's on-disk half: before Get Latest applies, the tree the update
would produce must leave no reference dangling, asked of `assetlib::AssetRefGraph`. It is a task of
its own rather than part of 3 because it is not backend knowledge — a second `IVersionControl`
must not have to reimplement it — so where the check sits relative to the seam is a design decision
worth reviewing on its own, and 3 is already the largest task here. *Gate:* `[vcs]` — an incoming
deletion of a material a local level still references is refused with both named and the tree left
byte-identical; the same deletion is allowed when the update deletes the referring level too; an
incoming deletion nothing references is allowed.

**5 — History and Undo.** `ListHistory(limit)` returning a `Submission` per entry — who, when, the
message, the assets it touched — and `UndoSubmission(id)`, which records the restoration as a new
submission (ADR-9). *Gate:* `[vcs]` — history newest first with the right assets per entry; Undo
restores the files and appends an entry rather than removing one; the undone submission is still
listed afterwards; Undo refuses, changing nothing, when the assets have moved on since, and again
when restoring them would leave a reference dangling.

**6 — the Version Control menu and its dialogs.** A top-level menu built in `MainWindow::Build` as
the Render menu is, with *Get Latest*, *Submit Changes…* and *History…*; `PendingChangesDialog`
(the change list with per-asset checkboxes, a message field, Submit, and Revert per asset) and
`HistoryDialog` (the submission list and Undo), each verb run through
`background::RunWithLoadingScreen`; the menu disabled with an explanatory tooltip when the project
is not in a repository. Whatever rule is liftable comes out as a free function and is tested — the
default Submit message is one. The open-asset half of ADR-10 lands here, since only the windows
know what they have open. *Gate:* the `[vcs]` suite still green, plus a written pass over
`bernini-test-project`: edit an asset in the editor, Submit, verify it on the remote, Get Latest in
a second clone, then Undo it.
