#pragma once
#include <core/file/IFileSystem.h>

namespace core::file
{
	/**
	 * A directory on disk, which is what every load in the project did before there was an interface
	 * to do it behind.
	 *
	 * Opens a fresh stream per call rather than holding one, so concurrent reads share no seek
	 * position and cost one `open` each -- the same cost the callers it replaces already paid.
	 *
	 * A path that would escape the root -- absolute, rooted, or normalizing to `..` or above -- is
	 * absent rather than resolved, so a reference out of a container cannot reach the rest of the
	 * disk.
	 */
	class LooseFileSystem final : public IFileSystem
	{
	public:
		/** `root` need not exist; every path is then simply absent. */
		explicit LooseFileSystem(std::filesystem::path root, bool readOnly = false) noexcept;

		LooseFileSystem(const LooseFileSystem&) = delete;
		LooseFileSystem(LooseFileSystem&&)      = delete;
		LooseFileSystem&
		operator=(const LooseFileSystem&) = delete;
		LooseFileSystem&
		operator=(LooseFileSystem&&) = delete;

		[[nodiscard]] const std::filesystem::path&
		Root() const noexcept
		{
			return m_Root;
		}

		[[nodiscard]] bool
		Exists(std::string_view path) const noexcept override;

		[[nodiscard]] std::optional<FileStamp>
		Stat(std::string_view path) const noexcept override;

		[[nodiscard]] std::vector<std::byte>
		Read(std::string_view path) const override;

		[[nodiscard]] std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const override;

		[[nodiscard]] std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const override;

		[[nodiscard]] bool
		IsReadOnly() const noexcept override
		{
			return m_ReadOnly;
		}

	private:
		// Empty when `path` would leave the root.
		[[nodiscard]] std::filesystem::path
		Resolve(std::string_view path) const;

		std::filesystem::path m_Root;
		bool                  m_ReadOnly;
	};
}
