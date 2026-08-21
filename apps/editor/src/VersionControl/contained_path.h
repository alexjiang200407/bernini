#pragma once

namespace editor
{
	/**
	 * `path` relative to `root`, or nothing when it does not resolve inside it.
	 *
	 * The one containment check this subsystem has. Two callers reach it from different directions --
	 * the fence a path crosses before it reaches a backend, and the question of whether a path is a
	 * project asset at all -- and two copies of it would be two chances to disagree about what
	 * "inside" means.
	 *
	 * Symlinks are resolved on both sides first: one root may arrive already resolved and the other as
	 * the user typed it, and compared as written those two put everything outside everything. A path
	 * that does not exist is still namable -- only the part of it that does exist resolves -- because
	 * an asset somebody else deleted still has to be nameable.
	 *
	 * Refuses `root` itself, which addresses the whole of something rather than anything in it, and a
	 * Windows drive-relative `D:foo`, which would re-root the join rather than extend it.
	 */
	[[nodiscard]] std::optional<std::filesystem::path>
	RelativeToRoot(const std::filesystem::path& root, const std::filesystem::path& path);
}
