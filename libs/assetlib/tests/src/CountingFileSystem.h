#pragma once
#include <core/file/IFileSystem.h>

namespace assetlib::test
{
	/**
	 * Wraps another filesystem and records what was actually read through it.
	 *
	 * The point of the seam is that a reference scan reads a container's header, its table and two
	 * small chunks rather than its megabytes of geometry. Nothing about the returned structs would
	 * show that guarantee being lost, so the test measures the reads themselves.
	 */
	class CountingFileSystem final : public core::file::IFileSystem
	{
	public:
		explicit CountingFileSystem(const core::file::IFileSystem& inner) : m_Inner(&inner) {}

		CountingFileSystem(const CountingFileSystem&) = delete;
		CountingFileSystem(CountingFileSystem&&)      = delete;
		CountingFileSystem&
		operator=(const CountingFileSystem&) = delete;
		CountingFileSystem&
		operator=(CountingFileSystem&&) = delete;

		mutable uint64_t bytesRead = 0;
		mutable uint32_t reads     = 0;

		[[nodiscard]] bool
		Exists(std::string_view path) const noexcept override
		{
			return m_Inner->Exists(path);
		}

		[[nodiscard]] std::optional<core::file::FileStamp>
		Stat(std::string_view path) const noexcept override
		{
			return m_Inner->Stat(path);
		}

		[[nodiscard]] std::vector<std::byte>
		Read(std::string_view path) const override
		{
			std::vector<std::byte> out = m_Inner->Read(path);
			bytesRead += out.size();
			++reads;
			return out;
		}

		[[nodiscard]] std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const override
		{
			bytesRead += size;
			++reads;
			return m_Inner->ReadRange(path, offset, size);
		}

		[[nodiscard]] std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const override
		{
			return m_Inner->Enumerate(prefix);
		}

		[[nodiscard]] bool
		IsReadOnly() const noexcept override
		{
			return m_Inner->IsReadOnly();
		}

	private:
		const core::file::IFileSystem* m_Inner;
	};
}
