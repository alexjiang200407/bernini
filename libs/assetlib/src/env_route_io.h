#pragma once
#include <assetlib_structs/SourceStamp.h>
#include <schema/LayoutBuilder.h>

namespace core
{
	class string_pool;
}

namespace assetlib
{
	struct EnvMapRoute;

	/**
	 * An EnvMapRoute as a container stores it: its two paths as pool offsets, its stamp inline.
	 *
	 * `.bsky` and `.benvl` hold the same route shape, so they describe, pack and unpack it with the
	 * same code: two containers that drifted by a field would each still load their own files and
	 * fail only when one was handed the other's.
	 */
	struct EnvRouteRecord
	{
		uint32_t    sourceOffset;
		uint32_t    bakedOffset;
		SourceStamp stamp;
	};

	static_assert(sizeof(EnvRouteRecord) == 24);

	/** The record's layout, named "EnvMapRoute". @pre the schema holds SourceStamp. */
	void
	describeEnvRoute(schema::LayoutBuilder<EnvRouteRecord>& layout);

	[[nodiscard]] EnvRouteRecord
	packRoute(const EnvMapRoute& route, core::string_pool& pool);

	[[nodiscard]] EnvMapRoute
	unpackRoute(const EnvRouteRecord& record, const core::string_pool& pool);
}
