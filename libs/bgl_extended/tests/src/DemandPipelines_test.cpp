#include "gfx/DrawBucketTable.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "passes/ForwardPass.h"
#include "passes/PassInitContext.h"
#include "passes/StaticDepthPass.h"
#include "pipeline/PipelineBatch.h"
#include "scene/SceneView.h"
#include "types/DrawBucketMask.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <bgl/Camera.h>
#include <bgl/GeomType.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <optional>

// Bucket pipelines are built by the first Draw that demands them, and never for a bucket nothing
// demands. The initialized set is read back from the RenderContext and compared against the view's
// own demand -- equality is the assertion, so an over-build (the old build-everything) and an
// under-build (a demanded bucket skipped) both fail. A demanded transparent bucket counts as
// initialized once the one shared blend kernel exists, so equality holds for it too.

namespace
{
	constexpr uint32_t c_Width  = 320;
	constexpr uint32_t c_Height = 240;

	// The id the table gave a key, read back by desc so the lookup cannot itself allocate.
	std::optional<uint32_t>
	FindDrawBucket(
		const bgl::DrawBucketTable& table,
		bgl::GeomType               geom,
		bgl::MaterialType           material,
		bgl::LayerType              layer)
	{
		for (uint32_t bucket = 0; bucket < table.Count(); ++bucket)
		{
			const bgl::DrawBucketDesc& desc = table.Desc(bucket);
			if (desc.geom == geom && desc.material == material && desc.layer == layer)
			{
				return bucket;
			}
		}
		return std::nullopt;
	}
}

TEST_CASE("Bucket pipelines are built on demand, and only on demand", "[pipeline][demand][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	const bgl::RenderContext* context = gfxBase->GetRenderContext();
	REQUIRE(context != nullptr);

	// Creation builds no bucket kernel: the always-on set carries no per-bucket pipeline.
	CHECK(context->InitializedDrawBuckets().none());

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 128;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 100000;
	sceneDesc.initialIndices              = 4000;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	const auto* sceneView = view->As<bgl::SceneView>();
	REQUIRE(sceneView != nullptr);

	// An empty view demands nothing, so a frame builds nothing.
	gfx->DrawFrame(target, job);
	CHECK(context->InitializedDrawBuckets().none());

	auto opaqueDesc            = bgl::PbrMaterialDesc();
	opaqueDesc.baseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	opaqueDesc.metallicFactor  = 0.0f;
	opaqueDesc.roughnessFactor = 1.0f;

	const auto opaque = scene->CreatePbrMaterial(opaqueDesc);
	const auto plane  = scene->AddPlaneGeom(1, 1, 4.0f, 4.0f, opaque);
	const auto placed = view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));
	(void)placed;

	gfx->DrawFrame(target, job);

	const bgl::DrawBucketTable& table = context->DrawBuckets();

	const auto opaqueBucket = FindDrawBucket(
		table,
		bgl::GeomType::kStaticMesh,
		bgl::MaterialType::kPBR,
		bgl::LayerType::kOpaque);
	REQUIRE(opaqueBucket.has_value());

	const bgl::DrawBucketMask afterOpaque = context->InitializedDrawBuckets();
	CHECK(afterOpaque == sceneView->DemandedDrawBuckets());
	CHECK(afterOpaque.test(*opaqueBucket));
	CHECK(afterOpaque.count() == 1);

	// A bucket demanded after frames have drawn is built by the next Draw -- the late-demand
	// path the old build-everything start-up never had.
	auto cutoutDesc            = bgl::PbrMaterialDesc();
	cutoutDesc.baseColorFactor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
	cutoutDesc.metallicFactor  = 0.0f;
	cutoutDesc.roughnessFactor = 1.0f;
	cutoutDesc.layerType       = bgl::LayerType::kMask;
	cutoutDesc.alphaCutoff     = 0.5f;

	const auto cutout = scene->CreatePbrMaterial(cutoutDesc);
	view->SetSubmeshMaterialOverride(placed, 0, cutout);

	gfx->DrawFrame(target, job);

	const auto cutoutBucket = FindDrawBucket(
		table,
		bgl::GeomType::kStaticMesh,
		bgl::MaterialType::kPBR,
		bgl::LayerType::kMask);
	REQUIRE(cutoutBucket.has_value());

	const bgl::DrawBucketMask afterCutout = context->InitializedDrawBuckets();
	CHECK(afterCutout == sceneView->DemandedDrawBuckets());
	CHECK(afterCutout.test(*cutoutBucket));
	CHECK(afterCutout.count() == 2);

	// The perf shape at the table: buckets -- and so the dispatch loops, which run to the table's
	// count -- scale with the distinct (tier, kind, layer) keys in use, never with the material
	// count or a fixed grid. Six more materials, each on its own placement, across the two keys
	// already drawn, allocate no bucket and build no kernel.
	const uint32_t bucketsBefore = table.Count();
	for (uint32_t i = 0; i < 6; ++i)
	{
		auto desc            = i % 2 == 0 ? opaqueDesc : cutoutDesc;
		desc.baseColorFactor = glm::vec4(0.1f * static_cast<float>(i), 0.5f, 0.5f, 1.0f);

		const auto material = scene->CreatePbrMaterial(desc);
		const auto geom     = scene->AddPlaneGeom(1, 1, 1.0f, 1.0f, material);
		(void)view->CreateStaticMeshInstance(
			geom,
			glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
	}

	gfx->DrawFrame(target, job);

	CHECK(table.Count() == bucketsBefore);
	CHECK(context->InitializedDrawBuckets() == afterCutout);
}

// Demand building means an ordinary run checks only the buckets its content uses, so a renamed
// member in a skinned or game-slot shader could pass every suite whose scenes are static. This
// is the case that keeps the binder-name check's old full coverage: build every bucket the way
// EnsureDrawBucketPipelinesExist would, then run the checks over the complete family.
TEST_CASE("Every bucket's binder names survive a full build", "[pipeline][demand][bindings]")
{
	// Surfaces registered, because a surface's programs exist only once it is: they are generated
	// at registration, so a device with none has no game kind to build.
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;
	opts.surfaceShaderDir = "./shaders/tests/surfaces";

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);
	REQUIRE_FALSE(gfx->GetSurfaceTypes().empty());

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto* device = gfxBase->GetDevice();

	// Every key a material can resolve to: each tier's every layer of every engine kind and every
	// registered surface's.
	const uint32_t       kinds = static_cast<uint32_t>(bgl::MaterialType::kGameStart) +
	                             static_cast<uint32_t>(gfx->GetSurfaceTypes().size());
	bgl::DrawBucketTable table;
	for (uint32_t kind = 0; kind < kinds; ++kind)
	{
		const auto material = static_cast<bgl::MaterialType>(kind);
		for (const auto layer : { bgl::LayerType::kOpaque,
		                          bgl::LayerType::kMask,
		                          bgl::LayerType::kBlend,
		                          bgl::LayerType::kHashed })
		{
			(void)table.Resolve(bgl::GeomType::kStaticMesh, material, layer);

			auto handle         = bgl::MaterialHandle();
			handle.materialType = material;
			handle.layerType    = layer;
			if (bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, handle))
			{
				(void)table.Resolve(bgl::GeomType::kSkinnedMesh, material, layer);
			}
		}
	}

	bgl::DrawBucketMask opaqueShaped;
	for (uint32_t bucket = 0; bucket < table.Count(); ++bucket)
	{
		opaqueShaped.set(bucket, !table.Transparent(bucket));
	}

	bgl::ForwardPass     forward;
	bgl::StaticDepthPass depth;

	auto       pipelines       = bgl::PipelineBatch(device);
	const auto resourceManager = gfxBase->GetResourceManagerCpy();
	const auto passes          = bgl::PassInitContext{ device, pipelines, resourceManager, table };
	forward.Init(passes);
	depth.Init(passes);
	forward.AddDrawBucketKernels(passes, opaqueShaped);
	forward.AddTransparentKernel(passes);
	depth.AddDrawBucketKernels(passes, opaqueShaped);
	pipelines.Build();

	for (uint32_t bucket = 0; bucket < table.Count(); ++bucket)
	{
		CHECK(forward.DrawBucketInitialized(bucket) == !table.Transparent(bucket));
	}
	CHECK(forward.TransparentInitialized());

	// gfatal on a binder name no built variant declares, which with every bucket built is the
	// original full check.
	forward.CheckBindings();
	depth.CheckBindings();

	forward.Release();
	depth.Release();
}
