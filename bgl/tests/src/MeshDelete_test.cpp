#include "idl/idl.h"
#include "scene/Scene.h"
#include <bgl/IGraphics.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.headless         = true;
		opts.width            = 64;
		opts.height           = 64;
		opts.wnd              = nullptr;
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	CubeSceneDesc()
	{
		auto desc         = bgl::SceneDesc();
		desc.maxInstances = 5;
		desc.maxGeom      = 5;
		desc.maxMeshlets  = 100;
		desc.maxVertices  = 1000;
		desc.maxIndices   = 1000;
		return desc;
	}
}

TEST_CASE("Buffer contents around mesh deletion", "[delete][buffers][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto sceneHandle = gfx->CreateScene(CubeSceneDesc());
	REQUIRE(sceneHandle != nullptr);

	auto* scene = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto material         = bgl::MaterialHandle();
	material.materialType = bgl::MaterialType::kPBR;

	auto geom = scene->AddCubeGeom();
	REQUIRE(geom.IsValid());

	auto inst = scene->CreateStaticMeshInstance(geom, material, glm::mat4(1.0f));
	REQUIRE(inst.IsValid());

	// std::tie-of-references: each binding is a reference to the live member.
	auto buffers = scene->GetAllBuffers();
	auto& [instanceBuffer, smiBuffer, geomBuffer, meshletBuffer, vertexMapBuffer, vertexBuffer, indexBuffer] =
		buffers;

	// Resolve the indices the instance chains through: instance -> static mesh
	// instance -> geom, and the geom's owned ranges.
	const auto&    baseInstance = instanceBuffer[inst.handle];
	const uint32_t smiIndex     = baseInstance.meshInstance.offset;
	const uint32_t geomIndex    = geom.handle.index;

	const auto&    staticGeom    = geomBuffer[geom.handle];
	const uint32_t vertexRoot    = staticGeom.vertices.offsetStart;
	const uint32_t indexRoot     = staticGeom.indices.offsetStart;
	const uint32_t meshletRoot   = staticGeom.meshlets.range.offsetStart;
	const uint32_t vertexMapRoot = staticGeom.vertexMap.offsetStart;

	SECTION("Contents before deletion")
	{
		// PackedBuffer: one live instance, reachable through its handle.
		CHECK(instanceBuffer.Count() == 1);
		CHECK(instanceBuffer.IsValid(inst.handle));

		// EntryBuffer (static mesh instance): live, and it points back at the geom.
		CHECK(smiBuffer.IsIndexValid(smiIndex));
		CHECK(smiBuffer.AtIndex(smiIndex).base.offset == geomIndex);
		CHECK(smiBuffer.AtIndex(smiIndex).transform[0][0] == 1.0f);
		CHECK(smiBuffer.AtIndex(smiIndex).transform[3][3] == 1.0f);

		// EntryBuffer (geom): live with a single reference from the instance.
		CHECK(geomBuffer.IsValid(geom.handle));
		CHECK(geomBuffer.MetaAt(geomIndex).refCount == 1);

		// RangeBuffers: the cube's geometry data is live.
		CHECK(vertexBuffer.IsIndexValid(vertexRoot));
		CHECK(indexBuffer.IsIndexValid(indexRoot));
		CHECK(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}

	SECTION("After DeleteMeshInstance the geom and its ranges survive")
	{
		scene->DeleteMeshInstance(inst);

		// PackedBuffer: the instance entry is gone and its handle is now stale.
		CHECK(instanceBuffer.Count() == 0);
		CHECK(instanceBuffer.IsEmpty());
		CHECK_FALSE(instanceBuffer.IsValid(inst.handle));

		// EntryBuffer (static mesh instance): its slot was freed.
		CHECK_FALSE(smiBuffer.IsIndexValid(smiIndex));

		// EntryBuffer (geom): retained, with the reference count dropped to zero.
		CHECK(geomBuffer.IsValid(geom.handle));
		CHECK(geomBuffer.MetaAt(geomIndex).refCount == 0);

		// RangeBuffers: untouched - deleting a mesh does not free geometry.
		CHECK(vertexBuffer.IsIndexValid(vertexRoot));
		CHECK(indexBuffer.IsIndexValid(indexRoot));
		CHECK(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}

	SECTION("After DeleteGeom the geometry ranges are freed")
	{
		scene->DeleteMeshInstance(inst);
		scene->DeleteGeom(geom);

		// EntryBuffer (geom): slot freed, handle stale.
		CHECK_FALSE(geomBuffer.IsValid(geom.handle));

		// RangeBuffers: every owned range is now freed.
		CHECK_FALSE(vertexBuffer.IsIndexValid(vertexRoot));
		CHECK_FALSE(indexBuffer.IsIndexValid(indexRoot));
		CHECK_FALSE(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK_FALSE(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}
}
