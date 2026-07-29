#include "string_io.h"

#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib
{
	void
	writeString(core::io::ByteWriter& writer, const std::string& value)
	{
		writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
		writer.writeBytes(std::as_bytes(std::span<const char>(value)));
	}

	std::string
	readString(core::io::ByteReader& reader)
	{
		const auto length = reader.readPod<uint32_t>();
		const auto bytes  = reader.readBytes(length);
		return std::string(reinterpret_cast<const char*>(bytes.data()), length);
	}
}
