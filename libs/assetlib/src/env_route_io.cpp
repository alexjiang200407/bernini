#include "env_route_io.h"

#include <assetlib_structs/BEnv.h>
#include <core/str/string_pool.h>

namespace assetlib
{
	void
	describeEnvRoute(schema::LayoutBuilder<EnvRouteRecord>& layout)
	{
		layout.AddField("sourceOffset", &EnvRouteRecord::sourceOffset)
			.AddField("bakedOffset", &EnvRouteRecord::bakedOffset)
			.AddField("stamp", &EnvRouteRecord::stamp);
	}

	EnvRouteRecord
	packRoute(const EnvMapRoute& route, core::string_pool& pool)
	{
		EnvRouteRecord record{};
		record.sourceOffset = pool.add(route.source);
		record.bakedOffset  = pool.add(route.baked);
		record.stamp        = route.stamp;
		return record;
	}

	EnvMapRoute
	unpackRoute(const EnvRouteRecord& record, const core::string_pool& pool)
	{
		EnvMapRoute route;
		route.source = pool.at(record.sourceOffset);
		route.baked  = pool.at(record.bakedOffset);
		route.stamp  = record.stamp;
		return route;
	}
}
