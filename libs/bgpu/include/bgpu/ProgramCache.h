#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	/**
	 * Who wrote an entry, and in which layout. It is half of every key, so an entry one owner wrote
	 * is a miss for another rather than bytes it would misread, and an owner that changes its layout
	 * bumps `formatVersion` and orphans only its own entries.
	 */
	struct ProgramCacheOwner
	{
		std::string_view tag;
		uint32_t         formatVersion = 0;
	};

	/**
	 * The program layer of the shader cache: what an owner compiled with the context's sessions,
	 * stored on disk under `GpuContextDesc::shaderCacheDir` and found again by what it was compiled
	 * from, so a warm run reaches no Slang session. Every owner of the device shares it; what an
	 * entry holds is each owner's own format, and the store keeps bytes.
	 *
	 * A key folds the owner, the compiler and every option the sessions compile with, the content of
	 * every file under the search paths, every registered source module, and the program's (module,
	 * entry point) pairs. A change to any of them misses every key it reaches: a stale entry is
	 * recompiled, never misread. The driver's pipeline cache is not here -- it needs the native
	 * device, so it stays with the owner that builds pipelines (docs/shader_cache.md).
	 *
	 * Safe from any thread: each key is its own file, published by an atomic rename.
	 */
	class ProgramCache
	{
	public:
		ProgramCache(const ProgramCache&) = delete;
		ProgramCache(ProgramCache&&)      = delete;

		ProgramCache&
		operator=(const ProgramCache&) = delete;

		ProgramCache&
		operator=(ProgramCache&&) = delete;

		/** Where the entries live, and where an owner keeps a file of its own beside them. */
		[[nodiscard]] virtual const std::filesystem::path&
		GetDirectory() const noexcept = 0;

		/**
		 * @param moduleEntries every (module, entry point) pair of the program, in any order.
		 * @throws std::runtime_error if a shader source cannot be read, until a key has succeeded.
		 */
		[[nodiscard]] virtual uint64_t
		ComputeKey(
			const ProgramCacheOwner&                         owner,
			std::vector<std::pair<std::string, std::string>> moduleEntries) const = 0;

		/**
		 * The bytes stored under `key`. False on a miss, and on an entry that is truncated, altered
		 * or filed under another key: the store checks its own files, so an owner decodes only
		 * bytes it stored.
		 */
		[[nodiscard]] virtual bool
		TryLoad(uint64_t key, std::vector<std::byte>& out) const noexcept = 0;

		/** A write that fails is logged, and the entry is compiled again next time. */
		virtual void
		Store(uint64_t key, std::span<const std::byte> bytes) const noexcept = 0;

	protected:
		ProgramCache() noexcept  = default;
		~ProgramCache() noexcept = default;
	};
}
