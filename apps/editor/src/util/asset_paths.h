#pragma once

#include <QString>

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
	 * Whether `path` names a file the Content Explorer's views do not list. Two kinds, for two
	 * different reasons:
	 *
	 * - a **`.bvat`**: a derived build product, wholly re-bakeable from its inputs, never
	 *   committed, and offering it for rename or delete implies an authorship it does not have.
	 *   Unreachable now that the views root inside `Authored/`, and kept because where they root is
	 *   a choice while this is a rule.
	 * - a **`.bimport`**: the sidecar carrying one source's import settings. The `.glb` beside it is
	 *   the row that stands for the model -- Unity hides a `.meta` and Godot a `.import` for the
	 *   same reason -- and listing both is two rows for one thing.
	 *
	 * The reference graph still reads both from the filesystem directly, so rename and delete
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
