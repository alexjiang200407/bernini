#pragma once

namespace core::io
{
	class ByteReader;
	class ByteWriter;
}

namespace assetlib
{
	/**
	 * How the flat containers (`.bsky`, `.benvl`, `.benv`) store a string: a uint32 length, then the raw bytes
	 * with no terminator.
	 *
	 * Shared rather than repeated per container. The encoding is not interesting, but three private
	 * copies of it are three chances for one to gain a terminator and only be noticed by the reader
	 * of a file written months earlier.
	 */
	void
	writeString(core::io::ByteWriter& writer, const std::string& value);

	[[nodiscard]] std::string
	readString(core::io::ByteReader& reader);
}
