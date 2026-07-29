#pragma once
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib
{
	/**
	 * How every container this library writes stores a string: a uint32 length, then the raw bytes
	 * with no terminator.
	 *
	 * Shared rather than repeated per container. The encoding is not interesting, but three private
	 * copies of it are three chances for one to gain a terminator and only be noticed by the reader
	 * of a file written months earlier.
	 */
	inline void
	writeString(core::io::ByteWriter& writer, const std::string& value)
	{
		writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
		writer.writeBytes(std::as_bytes(std::span<const char>(value)));
	}

	[[nodiscard]] inline std::string
	readString(core::io::ByteReader& reader)
	{
		const auto length = reader.readPod<uint32_t>();
		const auto bytes  = reader.readBytes(length);
		return std::string(reinterpret_cast<const char*>(bytes.data()), length);
	}
}
