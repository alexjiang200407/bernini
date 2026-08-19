#pragma once
#include <schema/SchemaBuilder.h>

namespace assetlib
{
	/**
	 * schema::SchemaBuilder with one registration per assetlib_structs POD that reaches disk, so a
	 * container's schema is assembled from the same descriptions whichever container holds the
	 * struct. A struct's parts go in first: Transform under Node and Bone, VertexLayout under
	 * Submesh -- each Add says what it needs. A container's private records come last, through
	 * AddLayout, which returns the base.
	 *
	 *     static const schema::Schema c_Schema =
	 *         AssetSchemaBuilder().AddTransform().AddBone().Finish();
	 */
	class AssetSchemaBuilder final : public schema::SchemaBuilder
	{
	public:
		AssetSchemaBuilder&
		AddTransform();

		/** @pre AddTransform. */
		AssetSchemaBuilder&
		AddNode();

		AssetSchemaBuilder&
		AddMesh();

		/** VertexAttribute, then VertexLayout. */
		AssetSchemaBuilder&
		AddVertexLayout();

		/** @pre AddVertexLayout. */
		AssetSchemaBuilder&
		AddSubmesh();

		AssetSchemaBuilder&
		AddMeshlet();

		/** @pre AddTransform. */
		AssetSchemaBuilder&
		AddBone();

		AssetSchemaBuilder&
		AddAnimationClip();

		AssetSchemaBuilder&
		AddSourceStamp();

		AssetSchemaBuilder&
		AddVatClip();

		AssetSchemaBuilder&
		AddVatColumns();
	};
}
