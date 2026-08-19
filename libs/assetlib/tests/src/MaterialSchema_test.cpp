#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/magic.h>

#include "chunk_io.h"

#include <core/str/string_pool.h>
#include <schema/SchemaBuilder.h>

using namespace assetlib;

/*
 * A .bmaterial as an earlier build would have written it: the PBR record before transmissionFactor
 * existed, with a narrower alpha mode and no stamps. The reader has never seen this layout; it
 * matches the fields it knows by name and defaults the rest, so the file reads as what it says.
 */
namespace
{
	struct OldMaterialRecord
	{
		uint32_t shadingModel;
		uint32_t nameOffset;
		uint32_t editorGraphOffset;
		uint32_t pad;
	};

	struct OldRouteRecord
	{
		uint32_t textureOffset;
		uint16_t channel;
		uint16_t pad;
	};

	struct OldPbrRecord
	{
		glm::vec4                     baseColorFactor;
		float                         metallicFactor;
		float                         roughnessFactor;
		uint8_t                       alphaMode;  // narrower than today's u32
		uint8_t                       pad[3];
		float                         alphaCutoff;
		uint32_t                      baseColorTextureOffset;
		uint32_t                      normalTextureOffset;
		uint32_t                      ormTextureOffset;
		std::array<OldRouteRecord, 9> routes;  // no stamps, no transmissionFactor
	};

	static_assert(sizeof(OldPbrRecord) == 44 + 9 * 8);

	schema::Schema
	OldSchema()
	{
		return schema::SchemaBuilder()
		    .AddLayout<OldMaterialRecord>(
				"MaterialRecord",
				[](auto& l) {
					l.AddField("shadingModel", &OldMaterialRecord::shadingModel)
						.AddField("nameOffset", &OldMaterialRecord::nameOffset)
						.AddField("editorGraphOffset", &OldMaterialRecord::editorGraphOffset)
						.AddField("pad", &OldMaterialRecord::pad);
				})
		    .AddLayout<OldRouteRecord>(
				"RouteRecord",
				[](auto& l) {
					l.AddField("textureOffset", &OldRouteRecord::textureOffset)
						.AddField("channel", &OldRouteRecord::channel)
						.AddField("pad", &OldRouteRecord::pad);
				})
		    .AddLayout<OldPbrRecord>(
				"PbrRecord",
				[](auto& l) {
					l.AddField("baseColorFactor", &OldPbrRecord::baseColorFactor)
						.AddField("metallicFactor", &OldPbrRecord::metallicFactor)
						.AddField("roughnessFactor", &OldPbrRecord::roughnessFactor)
						.AddField("alphaMode", &OldPbrRecord::alphaMode)
						.AddField("pad", &OldPbrRecord::pad)
						.AddField("alphaCutoff", &OldPbrRecord::alphaCutoff)
						.AddField("baseColorTextureOffset", &OldPbrRecord::baseColorTextureOffset)
						.AddField("normalTextureOffset", &OldPbrRecord::normalTextureOffset)
						.AddField("ormTextureOffset", &OldPbrRecord::ormTextureOffset)
						.AddField("routes", &OldPbrRecord::routes);
				})
		    .Finish();
	}
}

TEST_CASE(
	"a .bmaterial written under an older PBR layout reads, and defaults what it lacks",
	"[bmaterial][schema]")
{
	core::string_pool pool;
	OldMaterialRecord material{};
	material.shadingModel      = 0;  // kPbr
	material.nameOffset        = pool.add("glass");
	material.editorGraphOffset = pool.add("{}");

	OldPbrRecord pbr{};
	pbr.baseColorFactor         = glm::vec4(0.5f, 0.6f, 0.7f, 0.8f);
	pbr.metallicFactor          = 0.25f;
	pbr.roughnessFactor         = 0.75f;
	pbr.alphaMode               = 2;  // kBlend
	pbr.alphaCutoff             = 0.4f;
	pbr.baseColorTextureOffset  = pool.add("textures_src/glass.png");
	pbr.routes[3].textureOffset = pool.add("textures_src/glass_a.png");
	pbr.routes[3].channel       = 3;

	const auto    schema = OldSchema();
	chunk::Writer writer(schema);
	writer.Add(1u, std::vector<OldMaterialRecord>{ material });  // the reader's kMaterial
	writer.Add(2u, pool.bytes());                                // kStringPool
	writer.Add(3u, std::vector<OldPbrRecord>{ pbr });            // kPbr
	const auto bytes = writer.Finish(magic::c_BMaterial, 11, 0);

	const BMaterial loaded = deserializeMaterial(bytes);
	CHECK(loaded.name == "glass");
	CHECK(loaded.editorGraph == "{}");
	CHECK(loaded.shadingModel == ShadingModel::kPbr);
	CHECK(loaded.pbr.baseColorFactor == glm::vec4(0.5f, 0.6f, 0.7f, 0.8f));
	CHECK(loaded.pbr.metallicFactor == 0.25f);
	CHECK(loaded.pbr.alphaMode == AlphaMode::kBlend);  // widened from a byte
	CHECK(loaded.pbr.alphaCutoff == 0.4f);
	CHECK(loaded.pbr.transmissionFactor == 0.0f);  // never stored: the default
	CHECK(loaded.pbr.baseColorTexture == "textures_src/glass.png");
	CHECK(loaded.pbr.normalTexture.empty());
	CHECK(loaded.pbr.routes[3].texture == "textures_src/glass_a.png");
	CHECK(loaded.pbr.routes[3].channel == 3);
	CHECK(loaded.pbr.routes[0].texture.empty());
	CHECK(loaded.pbr.routeStamps[3] == SourceStamp{});  // never stored: zero

	// And what it reads as is what a save writes: the round trip through today's layout is stable.
	CHECK(
		deserializeMaterial(serializeMaterial(loaded)).pbr.routes[3].texture ==
		"textures_src/glass_a.png");
}
