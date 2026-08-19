#include <assetlib/container_info.h>

#include "chunk_io.h"

#include <schema/convert.h>

namespace assetlib
{
	ContainerInfo
	inspectContainer(std::span<const std::byte> bytes)
	{
		chunk::Inspection inspection = chunk::inspect(bytes, "container");
		return ContainerInfo{
			inspection.header.magic,
			inspection.header.versionMajor,
			inspection.header.versionMinor,
			std::move(inspection.stored),
		};
	}

	std::string
	describe(const schema::Schema& schema)
	{
		std::string out;
		for (const schema::Layout& layout : schema.GetLayouts())
		{
			out += std::format("  {} ({} bytes)\n", layout.name, layout.size);
			for (const schema::Field& field : layout.fields)
				out += std::format(
					"    {:<24} {:<28} @{}\n",
					field.name,
					schema::fieldShape(schema, field),
					field.offset);
		}
		return out;
	}
}
