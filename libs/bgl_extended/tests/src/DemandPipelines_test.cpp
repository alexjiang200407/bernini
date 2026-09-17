#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "passes/ForwardPass.h"
#include "passes/StaticDepthPass.h"
#include "pipeline/PipelineBatch.h"
#include "scene/SceneView.h"
#include "types/BucketMask.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/Viewport.h>
#include <bgl_common/idl/PsoType.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>

// Bucket pipelines are built by the first Draw that demands them, and never for a bucket nothing
// demands. The initialized set is read back from the RenderContext and compared against the view's
// own demand -- equality is the assertion, so an over-build (the old build-everything) and an
// under-build (a demanded bucket skipped) both fail. Equality holds for the buckets the passes
// bind directly, which is everything this scene demands; a transparent demand is substituted with
// the one shared blend bucket and would not compare equal.

namespace
{
	constexpr uint32_t c_Width  = 320;
	constexpr uint32_t c_Height = 240;
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
	CHECK(context->InitializedBuckets().none());

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
	CHECK(context->InitializedBuckets().none());

	auto opaqueDesc            = bgl::PbrMaterialDesc();
	opaqueDesc.baseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	opaqueDesc.metallicFactor  = 0.0f;
	opaqueDesc.roughnessFactor = 1.0f;

	const auto opaque = scene->CreatePbrMaterial(opaqueDesc);
	const auto plane  = scene->AddPlaneGeom(1, 1, 4.0f, 4.0f, opaque);
	const auto placed = view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));
	(void)placed;

	gfx->DrawFrame(target, job);

	const bgl::BucketMask afterOpaque = context->InitializedBuckets();
	CHECK(afterOpaque == sceneView->DemandedBuckets());
	CHECK(afterOpaque.test(static_cast<size_t>(bgl::idl::PsoType::kOpaque_StaticMesh_PBR)));
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

	const bgl::BucketMask afterCutout = context->InitializedBuckets();
	CHECK(afterCutout == sceneView->DemandedBuckets());
	CHECK(afterCutout.test(static_cast<size_t>(bgl::idl::PsoType::kAlphaTest_StaticMesh_PBR)));
	CHECK(afterCutout.count() == 2);
}

// Demand building means an ordinary run checks only the buckets its content uses, so a renamed
// member in a skinned or game-slot shader could pass every suite whose scenes are static. This
// is the case that keeps the binder-name check's old full coverage: build every bucket the way
// EnsureBucketPipelines would, then run the checks over the complete family.
TEST_CASE("Every bucket's binder names survive a full build", "[pipeline][demand][bindings]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto* device = gfxBase->GetDevice();

	bgl::ForwardPass     forward;
	bgl::StaticDepthPass depth;

	auto pipelines = bgl::PipelineBatch(device);
	forward.Init(device, pipelines);
	depth.Init(device, pipelines);
	forward.AddBucketKernels(device, pipelines, bgl::BucketMask().set());
	depth.AddBucketKernels(device, pipelines, bgl::BucketMask().set());
	pipelines.Build();

	for (uint16_t pso = 0; pso < bgl::idl::c_PsoCount; ++pso)
	{
		CHECK(forward.BucketInitialized(pso));
	}

	// gfatal on a binder name no built variant declares, which with every bucket built is the
	// original full check.
	forward.CheckBindings();
	depth.CheckBindings();

	forward.Release();
	depth.Release();
}
