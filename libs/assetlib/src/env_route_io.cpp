#include "env_route_io.h"

#include "string_io.h"

#include <assetlib_structs/BEnv.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib
{
	void
	writeRoute(core::io::ByteWriter& writer, const EnvMapRoute& route)
	{
		writeString(writer, route.source);
		writeString(writer, route.baked);
		writer.WritePod(route.stamp.size);
		writer.WritePod(route.stamp.mtime);
	}

	EnvMapRoute
	readRoute(core::io::ByteReader& reader)
	{
		EnvMapRoute route;
		route.source      = readString(reader);
		route.baked       = readString(reader);
		route.stamp.size  = reader.ReadPod<uint64_t>();
		route.stamp.mtime = reader.ReadPod<int64_t>();
		return route;
	}
}
