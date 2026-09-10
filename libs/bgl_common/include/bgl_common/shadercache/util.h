#pragma once
#include <bgl_common/ReflectedLayout.h>

#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The parts of the persistent shader cache that hold whatever the backend stores in it: the
// invalidation salt, the key hash, the reflection encoding and the atomic write. Each backend owns
// its own ShaderCache around these, because what a cache entry *contains* -- DXIL or MSL, a root
// parameter index or a per-stage buffer index -- is its private business. See docs/shader_cache.md.
namespace bgl::shader_cache
{
	// One hash over the compile options and the content of every shader source file, so any edit to
	// a shader -- or a change of compiler, options or format version -- moves every derived key and
	// a stale entry is missed rather than misread.
	uint64_t
	ComputeSourceSalt(
		std::string_view                optionsSalt,
		const std::vector<std::string>& searchPaths,
		uint32_t                        formatVersion);

	// Combines a module given as text into `salt` -- name and content both, mixed in a way that
	// does not depend on the order modules arrive in, so two registered the other way round still
	// share a salt. The same mixing the walk over the source files does.
	uint64_t
	FoldSource(uint64_t salt, std::string_view name, std::string_view source);

	// Combines a PSO's (module, entry-point) pairs into `salt`, order-independently as above.
	uint64_t
	ComputeKey(uint64_t salt, std::vector<std::pair<std::string, std::string>> moduleEntries);

	std::filesystem::path
	KeyPath(const std::filesystem::path& dir, uint64_t key);

	// Writes via a temp file then renames, so a crash mid-write never leaves a half-written file
	// that would later look valid. The temp name carries the process id because several processes
	// may share one cache directory -- a sharded test run does.
	bool
	WriteFileAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes);

	void
	WriteString(core::io::ByteWriter& writer, std::string_view value);

	std::string
	ReadString(core::io::ByteReader& reader);

	void
	WriteBlob(core::io::ByteWriter& writer, std::span<const std::byte> value);

	std::vector<std::byte>
	ReadBlob(core::io::ByteReader& reader);

	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout);

	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader);
}
