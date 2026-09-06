#include "scene/Scene.h"
#include "util/SkinnedSynth.h"
#include "util/TestOptions.h"
#include <RangeWithCount.h>
#include <array>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/Geom.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// The geom's own GPU record. Nothing reads it yet -- a placement still carries the submesh range by
// value -- so these cases are what holds it to the range Scene keeps on the CPU until the reader
// lands. They read the CPU mirror, which is exactly the bytes Update() uploads.

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	TestSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialSubmeshes            = 8;
		desc.initialMeshlets             = 32;
		desc.initialVertexBufferByteSize = 65536;
		desc.initialIndices              = 1024;
		desc.initialPbrMaterials         = 4;
		return desc;
	}

	bgl::MaterialHandle
	OpaquePbr(bgl::Scene* scene)
	{
		return scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	}

	// The record the geom uploaded, reached the way a placement will reach it.
	const bgl::idl::Geom&
	RecordOf(bgl::Scene* scene, bgl::GeomHandle geom)
	{
		return scene->GetGeomBuffer()[scene->GetGeomEntry(geom.handle.index)];
	}
}

TEST_CASE("Every geom kind uploads a record naming its submeshes", "[geom]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto material = OpaquePbr(scene);

	const auto geoms = std::array{
		scene->AddCubeGeom(material),
		scene->AddSphereGeom(8, 8, 1.0f, material),
		scene->AddPlaneGeom(2, 2, 1.0f, 1.0f, material),
		bgl::test::skinned_synth::AddSlidingQuadGeom(*scene, material),
	};

	for (const bgl::GeomHandle geom : geoms)
	{
		REQUIRE(geom.IsValid());

		const bgl::idl::RangeWithCount& onCpu = scene->GetGeomSubmeshes(geom.handle.index);
		const bgl::idl::Geom&           onGpu = RecordOf(scene, geom);

		CHECK(onGpu.submeshes.count == onCpu.count);
		CHECK(onGpu.submeshes.range.offsetStart == onCpu.range.offsetStart);

		// Element 0 is the arena's reserved null, so a live geom never lands on it -- which is what
		// lets a placement's Entry<Geom> mean "no geom" by being zero.
		CHECK(scene->GetGeomEntry(geom.handle.index).index != 0);
	}
}

TEST_CASE("Two geoms hold two records", "[geom]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto material = OpaquePbr(scene);

	const auto first  = scene->AddCubeGeom(material);
	const auto second = scene->AddCubeGeom(material);

	CHECK(
		scene->GetGeomEntry(first.handle.index).index !=
		scene->GetGeomEntry(second.handle.index).index);

	// Identical geometry, but each owns its submeshes, so the ranges differ too.
	CHECK(
		RecordOf(scene, first).submeshes.range.offsetStart !=
		RecordOf(scene, second).submeshes.range.offsetStart);
}

TEST_CASE("A deleted geom gives its record back", "[geom]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto material = OpaquePbr(scene);

	const auto     first      = scene->AddCubeGeom(material);
	const uint32_t firstEntry = scene->GetGeomEntry(first.handle.index).index;

	scene->DeleteGeom(first);

	// The freed slot is the one the next geom takes. Proving the release this way rather than by
	// asking the buffer keeps the assertion on behaviour a caller can see: a scene that creates and
	// destroys geometry all session does not grow a record per geom it ever held.
	const auto second = scene->AddCubeGeom(material);
	CHECK(scene->GetGeomEntry(second.handle.index).index == firstEntry);
}
