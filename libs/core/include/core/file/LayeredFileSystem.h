#pragma once
#include <core/file/IFileSystem.h>
#include <core/str/str.h>

namespace core::file
{
	/**
	 * Several filesystems in order, resolving each path to the first that carries it.
	 *
	 * One rule, four uses: the editor's loose overlay shadowing the archive it was opened from, a
	 * debug build reading an unpacked file dropped beside the executable, a patch shipped ahead of
	 * a base archive, and a mod doing the same.
	 *
	 * A list rather than a loose/archive pair, even though two is the only shape anything mounts
	 * today. A pair puts the policy in the type: a standalone baked model directory and the
	 * golden-image tests have no archive, so one half would be empty, and every consumer would then
	 * branch on which halves are present. A list of one is a directory, a list of two is the
	 * editor, and the patch and mod cases need no new type.
	 *
	 * Itself an IFileSystem, so a consumer takes one thing and neither knows nor cares how many
	 * mounts are behind it.
	 */
	class LayeredFileSystem final : public IFileSystem
	{
	public:
		/** Appends `mount` as the *lowest* priority. Null is ignored. */
		void
		Mount(std::shared_ptr<IFileSystem> mount);

		[[nodiscard]] std::span<const std::shared_ptr<IFileSystem>>
		Mounts() const noexcept
		{
			return m_Mounts;
		}

		/**
		 * Paths to report as absent however many mounts carry them -- the editor's record of what has
		 * been deleted out of an archive, which cannot be unlinked from it.
		 *
		 * Replaces the whole set, because that is how it arrives: one manifest read from disk.
		 */
		void
		SetMask(std::span<const std::string> paths);

		[[nodiscard]] bool
		IsMasked(std::string_view path) const noexcept;

		/** The mount that answers `path`, or null when none does or it is masked. */
		[[nodiscard]] const IFileSystem*
		Resolve(std::string_view path) const noexcept;

		/**
		 * Whether the mount answering `path` is read-only -- the question a caller regenerating a
		 * derived file asks, and not the same as IsReadOnly(): a loose overlay over an archive is
		 * writable for a path the overlay carries and read-only for one only the archive does.
		 *
		 * Falls back to IsReadOnly() when nothing answers.
		 */
		[[nodiscard]] bool
		IsPathReadOnly(std::string_view path) const noexcept;

		[[nodiscard]] bool
		Exists(std::string_view path) const noexcept override;

		[[nodiscard]] std::optional<FileStamp>
		Stat(std::string_view path) const noexcept override;

		[[nodiscard]] std::vector<std::byte>
		Read(std::string_view path) const override;

		[[nodiscard]] std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const override;

		/** The union across every mount, masked paths removed, each path once. */
		[[nodiscard]] std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const override;

		/** True unless some mount can be written. An empty search path is read-only. */
		[[nodiscard]] bool
		IsReadOnly() const noexcept override;

	private:
		std::vector<std::shared_ptr<IFileSystem>> m_Mounts;

		std::unordered_set<std::string, str::transparent_string_hash, std::equal_to<>> m_Mask;
	};
}
