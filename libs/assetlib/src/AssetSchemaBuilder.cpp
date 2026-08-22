#include "AssetSchemaBuilder.h"

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceStamp.h>
#include <assetlib_structs/VertexLayout.h>

namespace assetlib
{
	AssetSchemaBuilder&
	AssetSchemaBuilder::AddSourceStamp()
	{
		AddLayout<SourceStamp>("SourceStamp", [](auto& layout) {
			layout.AddField("size", &SourceStamp::size).AddField("hash", &SourceStamp::hash);
		});
		return *this;
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddVatClip()
	{
		AddLayout<VatClip>("VatClip", [](auto& layout) {
			layout.AddField("nameOffset", &VatClip::nameOffset)
				.AddField("firstRow", &VatClip::firstRow)
				.AddField("frameCount", &VatClip::frameCount)
				.AddField("firstPalette", &VatClip::firstPalette)
				.AddField("sampleRate", &VatClip::sampleRate)
				.AddField("duration", &VatClip::duration)
				.AddField("loop", &VatClip::loop);
		});
		return *this;
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddVatColumns()
	{
		AddLayout<VatColumns>("VatColumns", [](auto& layout) {
			layout.AddField("columnBase", &VatColumns::columnBase)
				.AddField("vertexCount", &VatColumns::vertexCount);
		});
		return *this;
	}
}
