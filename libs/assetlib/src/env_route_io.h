#pragma once

namespace core::io
{
	class ByteReader;
	class ByteWriter;
}

namespace assetlib
{
	struct EnvMapRoute;

	/**
	 * An EnvMapRoute's encoding, shared by every container that stores one.
	 *
	 * `.bsky` and `.benvl` hold the same route shape, so they read and write it with the same code:
	 * two containers that drifted by a field would each still load their own files and fail only when
	 * one was handed the other's.
	 */
	void
	writeRoute(core::io::ByteWriter& writer, const EnvMapRoute& route);

	[[nodiscard]] EnvMapRoute
	readRoute(core::io::ByteReader& reader);
}
