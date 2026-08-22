#pragma once
#include <assetlib_structs/SourceRef.h>
#include <schema/Schema.h>

namespace assetlib
{
	/** What a container says about itself before its payload is read. */
	struct ContainerInfo
	{
		uint32_t       magic;
		uint16_t       versionMajor;
		uint16_t       versionMinor;
		schema::Schema schema;  // the layouts the file was written with
	};

	/**
	 * The header and schema of any of this library's chunk containers, whatever its type -- for a
	 * tool that asks what a file is rather than loads it. Reads the header, the chunk table and the
	 * schema chunk; never a payload.
	 *
	 * @throws std::runtime_error on a malformed table, or a file from before the schema chunk.
	 */
	[[nodiscard]] ContainerInfo
	inspectContainer(std::span<const std::byte> bytes);

	/**
	 * Whether `bytes` opens as a JSON object -- an authored text document rather than a
	 * magic-headed container. Leading whitespace is skipped, exactly as the loaders skip it, so a
	 * tool dispatching on this agrees with what the engine will read.
	 */
	[[nodiscard]] bool
	isTextAssetDocument(std::span<const std::byte> bytes) noexcept;

	/** A geometry cache entry's identity: the token it was written at, and its source. */
	struct CacheEntryInfo
	{
		uint32_t  magic     = 0;
		uint64_t  bakeToken = 0;
		SourceRef source;
	};

	/**
	 * The cache key of a `.bmesh`, `.bskel` or `.banim`, or nullopt for any other magic -- the
	 * geometry containers carry a key where the others carry a schema, and this is the half of
	 * `describe --schema` that serves them.
	 */
	[[nodiscard]] std::optional<CacheEntryInfo>
	inspectCacheEntry(std::span<const std::byte> bytes);

	/**
	 * A schema as text, one layout per line with its fields and their shapes -- what `describe`
	 * prints so a person can see what an older file actually stores. For a person, not a parser.
	 */
	[[nodiscard]] std::string
	describe(const schema::Schema& schema);
}
