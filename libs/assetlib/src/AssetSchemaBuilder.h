#pragma once
#include <schema/SchemaBuilder.h>

namespace assetlib
{
	/**
	 * schema::SchemaBuilder with one registration per assetlib_structs POD that still reaches disk
	 * through a schema container (.bmaterial, .bvat and the env family -- geometry moved to the
	 * cache format), so a container's schema is assembled from the same descriptions whichever
	 * container holds the struct. A container's private records come last, through AddLayout,
	 * which returns the base.
	 *
	 *     static const schema::Schema c_Schema =
	 *         AssetSchemaBuilder().AddSourceStamp().AddVatClip().Finish();
	 */
	class AssetSchemaBuilder final : public schema::SchemaBuilder
	{
	public:
		AssetSchemaBuilder&
		AddSourceStamp();

		AssetSchemaBuilder&
		AddVatClip();

		AssetSchemaBuilder&
		AddVatColumns();
	};
}
