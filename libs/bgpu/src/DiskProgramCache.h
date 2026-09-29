#pragma once
#include "SlangSessions.h"
#include <bgpu/ProgramCache.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bgpu
{
	/**
	 * The context's program cache: one `<key>.bsc` file per entry, a header the store checks and the
	 * owner's bytes after it.
	 */
	class DiskProgramCache final : public ProgramCache
	{
	public:
		/** @param sessions outlives this; its options, search paths and modules are in every key. */
		DiskProgramCache(std::filesystem::path directory, const SlangSessions& sessions);

		~DiskProgramCache() noexcept = default;

		DiskProgramCache(const DiskProgramCache&) = delete;
		DiskProgramCache(DiskProgramCache&&)      = delete;

		DiskProgramCache&
		operator=(const DiskProgramCache&) = delete;

		DiskProgramCache&
		operator=(DiskProgramCache&&) = delete;

		[[nodiscard]] const std::filesystem::path&
		GetDirectory() const noexcept override
		{
			return m_Directory;
		}

		[[nodiscard]] uint64_t
		ComputeKey(
			const ProgramCacheOwner&                         owner,
			std::vector<std::pair<std::string, std::string>> moduleEntries) const override;

		[[nodiscard]] bool
		TryLoadProgram(uint64_t key, std::vector<std::byte>& program) const noexcept override;

		void
		StoreProgram(uint64_t key, std::span<const std::byte> program) const noexcept override;

	private:
		[[nodiscard]] std::filesystem::path
		EntryPath(uint64_t key) const;

		// Fixed for the context's life and a walk of the whole shader tree, so taken once.
		[[nodiscard]] uint64_t
		ContextSalt() const;

		std::filesystem::path m_Directory;
		const SlangSessions&  m_Sessions;

		mutable std::once_flag m_ContextSaltOnce;
		mutable uint64_t       m_ContextSalt = 0;
	};
}
