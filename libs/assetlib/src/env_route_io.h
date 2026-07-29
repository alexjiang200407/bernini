#pragma once
#include <assetlib_structs/BEnv.h>

#include "string_io.h"

namespace assetlib
{
	/**
	 * An EnvMapRoute's encoding, shared by every container that stores one.
	 *
	 * `.bsky` and `.benvl` hold the same route shape, so they read and write it with the same code:
	 * two containers that drifted by a field would each still load their own files and fail only when
	 * one was handed the other's.
	 */
	inline void
	writeRoute(core::io::ByteWriter& writer, const EnvMapRoute& route)
	{
		writeString(writer, route.source);
		writeString(writer, route.baked);
		writer.writePod(route.stamp.size);
		writer.writePod(route.stamp.mtime);
	}

	[[nodiscard]] inline EnvMapRoute
	readRoute(core::io::ByteReader& reader)
	{
		EnvMapRoute route;
		route.source      = readString(reader);
		route.baked       = readString(reader);
		route.stamp.size  = reader.readPod<uint64_t>();
		route.stamp.mtime = reader.readPod<int64_t>();
		return route;
	}
}
