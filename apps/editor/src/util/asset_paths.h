#pragma once

#include <QString>
#include <qtypes.h>

namespace editor
{
	/**
	 * Modification time of `path` in milliseconds, or 0 when it cannot be read.
	 *
	 * What the editor's caches compare to decide whether what they hold still describes the file. The
	 * resolution is the caller's problem: two writes inside one millisecond share a stamp, so anything
	 * that has just rewritten a file invalidates rather than trusting this to have moved.
	 */
	[[nodiscard]] qint64
	FileStamp(const QString& path);

	/**
	 * Whether `path` names a texture asset (`.ktx2`, case-insensitively), whatever the file's
	 * contents turn out to be.
	 */
	[[nodiscard]] bool
	IsTextureFile(const QString& path);

	/**
	 * Whether `path` names a file the Content Explorer's views do not list: a sidecar whose row is
	 * the file beside it -- as Unity hides a `.meta` and Godot a `.import`.
	 *
	 * The reference graph still reads these from the filesystem directly, so rename and delete
	 * cascades of their *inputs* see them.
	 */
	[[nodiscard]] bool
	IsHiddenInExplorer(const QString& path);

	/**
	 * Whether `name` is a plain file stem -- letters, digits, `_`, `.` and `-`, and not `.` or `..`.
	 *
	 * An import joins a typed name onto a category directory and a suffix, so anything that could
	 * redirect that join names a file outside the layout the project guarantees.
	 */
	[[nodiscard]] bool
	IsPlainFileStem(const QString& name);

	/**
	 * `name` reduced to something IsPlainFileStem accepts: trimmed, every character it rejects replaced
	 * with `_`, and leading dots dropped -- they would hide the file, or name a directory.
	 *
	 * Empty when nothing survives, which a caller names by some other means. Free text from a source
	 * asset goes through here rather than being checked for the same characters again, so a name that
	 * is derived cannot fail the validation a typed one is held to.
	 */
	[[nodiscard]] QString
	ToPlainFileStem(const QString& name);

	/**
	 * `path` as a key relative to `root` -- `/`-separated, never climbing out -- or empty when
	 * `path` does not lie inside `root`. `root` itself answers `"."`, since a directory contains
	 * itself and a caller that cares can tell the two apart.
	 *
	 * **The one statement of "is this inside that."** `QDir::relativeFilePath` answers for a path
	 * anywhere on the host by climbing out with `../`, and `QDir::filePath` reattaches such a
	 * result without cleaning it -- so a path outside a root resolves straight back into it, and
	 * only fails to when a directory along the climb happens not to exist. Every caller that
	 * compared `startsWith("..")` by hand got a slightly different answer, and one of them
	 * (`AssetAt`) rejected a folder legitimately named `..hidden`.
	 *
	 * Deliberately *not* IsContainedRelativePath below, which answers a different question: that
	 * one validates a **typed** relative path before it is joined onto a category, so it also
	 * refuses `:` and a leading separator -- spellings a person can enter and `relativeFilePath`
	 * cannot return. Applying it here would make a file whose name contains `:` read as outside its
	 * own root, and one caller of this is the gate on every deletion.
	 */
	[[nodiscard]] QString
	GetKeyUnder(const QString& root, const QString& path);

	/**
	 * Whether `path` lies inside `root`: GetKeyUnder's answer with the key thrown away, for a caller
	 * that only asks. One rule, two spellings of it -- a caller that needs the key must not have to
	 * ask twice, and a caller that does not must not have to write `.isEmpty()` to mean "inside".
	 */
	[[nodiscard]] bool
	IsKeyUnder(const QString& root, const QString& path);

	/**
	 * Whether `path` is a relative folder that cannot climb out of whatever it is joined onto.
	 *
	 * Rejects an absolute path, a leading separator, a drive-relative `D:` -- which
	 * `std::filesystem::path::operator/=` would use to re-root the join, and which
	 * `QDir::isAbsolutePath` does not call absolute -- and anything that normalizes to `..` or above.
	 */
	[[nodiscard]] bool
	IsContainedRelativePath(const QString& path);

	/**
	 * `category/<path>`, or the bare category when `path` is not a contained relative folder.
	 *
	 * The category is the fixed part: every reference in a project is written against that layout, so
	 * a typed folder organises inside one and can never move an asset out of it.
	 */
	[[nodiscard]] QString
	JoinCategory(const QString& category, const QString& path);
}
