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
	 * Whether `name` is a plain file stem -- letters, digits, `_`, `.` and `-`, and not `.` or `..`.
	 *
	 * An import joins a typed name onto a category directory and a suffix, so anything that could
	 * redirect that join names a file outside the layout the project guarantees.
	 */
	[[nodiscard]] bool
	IsPlainFileStem(const QString& name);

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
