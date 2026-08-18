#pragma once
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
	 * A schema as text, one layout per line with its fields and their shapes -- what `describe`
	 * prints so a person can see what an older file actually stores. For a person, not a parser.
	 */
	[[nodiscard]] std::string
	describe(const schema::Schema& schema);
}
