#pragma once
#include "uniforms/ReflectedLayout.h"

#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

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

	// Folds a PSO's (module, entry-point) pairs into `salt`. Order-independent.
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
