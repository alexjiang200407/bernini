#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/DrawBucketTable.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "passes/BrdfLutGenPass.h"
#include "passes/PassInitContext.h"
#include "pipeline/PipelineBatch.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/QueueType.h"
#include "util/HalfFloat.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
	constexpr uint32_t c_Dimension = 256;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	struct Texel
	{
		float scale;  // multiplies F0
		float bias;   // added to it
	};

	/**
	 * The table as one Texel per texel, row-major. Row v is roughness, column u is dot(N,V), each
	 * read at the texel centre -- so row 0 is roughness 1/512, not 0.
	 */
	std::vector<Texel>
	ReadTable(bgl::IGraphics* gfx)
	{
		auto* gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto  resourceManager = gfxBase->GetResourceManagerCpy();
		auto* device          = gfxBase->GetDevice();

		// This suite's own table, not the RenderContext's: the point is to exercise the generation,
		// and a second one costs a single 256x256 draw.
		auto lut         = bgl::BrdfLutGenPass();
		auto pipelines   = bgl::PipelineBatch(device);
		auto drawBuckets = bgl::DrawBucketTable();
		lut.Init(bgl::PassInitContext{ device, &pipelines, resourceManager, &drawBuckets });
		pipelines.Build();

		// Nothing exists until Generate: laziness is the pass's contract now, and the texture the
		// readback below copies is created by the recording itself.
		REQUIRE_FALSE(lut.Generated());

		auto cmdQueue = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		// Registered so Generate's deferred RTV free gates on this queue's timeline; without it
		// the free could reclaim the slot while the submission still draws through it.
		resourceManager->RegisterQueue(cmdQueue.Get());

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList =
			device->CreateCommandList({ bgl::QueueType::kGraphics }, cmdAllocator, resourceManager);

		cmdList->Open(cmdQueue.Get(), cmdAllocator.Get());
		lut.Generate(cmdList.Get());
		REQUIRE(lut.Generated());

		const auto layout = resourceManager->GetTextureReadbackLayout(lut.GetTexture());

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = layout.totalBytes;
		rbDesc.debugName = "BRDF LUT Readback";

		auto readback = resourceManager->CreateReadbackBuffer(rbDesc);

		auto toCopySource = bgl::TextureBarrierDesc();
		toCopySource.AddSyncBefore(bgl::BarrierSyncFlag::kAllCommands)
			.AddAccessBefore(bgl::BarrierAccessFlag::kShaderResource)
			.SetLayoutBefore(bgl::BarrierLayout::kShaderResource)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource)
			.SetLayoutAfter(bgl::BarrierLayout::kCopySource);
		cmdList->Barrier(lut.GetTexture(), toCopySource);

		cmdList->CopyTextureToReadback(readback, lut.GetTexture());
		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));
		REQUIRE(base != nullptr);

		auto table = std::vector<Texel>(static_cast<size_t>(c_Dimension) * c_Dimension);
		for (uint32_t y = 0; y < c_Dimension; ++y)
		{
			const auto* row =
				reinterpret_cast<const uint16_t*>(base + layout.offset + y * layout.rowPitch);

			for (uint32_t x = 0; x < c_Dimension; ++x)
			{
				table[static_cast<size_t>(y) * c_Dimension + x] = {
					bgl::test::HalfToFloat(row[x * 2]),
					bgl::test::HalfToFloat(row[x * 2 + 1]),
				};
			}
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		lut.Release();
		resourceManager->UnregisterQueue(cmdQueue.Get());

		return table;
	}

	// The value each axis carries at texel `i`, sampled at its centre.
	constexpr float
	AxisAt(uint32_t i) noexcept
	{
		return (static_cast<float>(i) + 0.5f) / static_cast<float>(c_Dimension);
	}
}

// Generated rather than shipped, so nothing on disk pins these values -- which makes the analytic
// identities below the only thing standing between a broken integral and a scene that merely looks
// a bit off. Each one fails differently, and each catches a mistake the golden images would not
// localize.
TEST_CASE("The generated BRDF table satisfies the split-sum identities", "[brdflut][render]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	const std::vector<Texel> table = ReadTable(gfx.Get());
	REQUIRE(table.size() == static_cast<size_t>(c_Dimension) * c_Dimension);

	SECTION("At mirror roughness the two terms sum to exactly one")
	{
		// The half vector is the normal at every sample, so the distribution and the pdf cancel and
		// the row reduces to Fresnel times masking: scale + bias = G, bias = (1-NdotV)^5 * G. It is
		// the one row with a closed form, which makes it pin three things at once -- the axes (a
		// swapped u and v gives a roughness sweep, which does not fit), the normalization, and the
		// Smith remapping, since G is where k appears.
		//
		// G rather than 1: roughness at the row's texel centre is small but not zero, and in the
		// first column k is within a thousandth of NdotV, so the masking is measurably below one.
		const float alpha = AxisAt(0) * AxisAt(0);
		const float k     = alpha * 0.5f;

		for (uint32_t x = 0; x < c_Dimension; ++x)
		{
			const Texel texel = table[x];
			const float nv    = AxisAt(x);
			const float g1    = nv / (nv * (1.0f - k) + k);
			const float g     = g1 * g1;  // NdotL equals NdotV when the half vector is the normal
			const float fc    = std::pow(1.0f - nv, 5.0f);

			CHECK(texel.scale + texel.bias == Catch::Approx(g).margin(2e-3));
			CHECK(texel.bias == Catch::Approx(fc * g).margin(2e-3));
		}
	}

	SECTION("Every texel is finite and non-negative")
	{
		// NaN is the failure this integral actually has: at a roughness near zero the GGX inversion
		// cancels to a cosine just above one, and the sine is then the root of a negative. It ruins
		// exactly the mirror row, which is the row a golden image of a rough scene never samples.
		for (const Texel texel : table)
		{
			REQUIRE(std::isfinite(texel.scale));
			REQUIRE(std::isfinite(texel.bias));
			CHECK(texel.scale >= 0.0f);
			CHECK(texel.bias >= 0.0f);
		}
	}

	SECTION("No texel gains energy")
	{
		// scale + bias is the fraction of a white environment the surface returns, so it cannot
		// exceed one anywhere -- including at grazing incidence, where the masking term is doing the
		// most work and an error in it is least visible.
		//
		// This is what catches a wrong Smith remapping. k = a/2 is the IBL one; the direct-light
		// (a+1)^2/8 and the plausible-looking a^2/2 both under-shadow, and a^2/2 takes the first
		// column to 8. Every one of those still leaves a table that looks right on a rough sphere.
		for (const Texel texel : table)
		{
			CHECK(texel.scale + texel.bias <= 1.0f + 2e-3f);
		}
	}

	SECTION("The table is not a cleared buffer")
	{
		// A pipeline that never ran leaves the clear colour, which satisfies every bound above.
		const Texel mirror = table[c_Dimension - 1];  // sharp, head-on
		const Texel rough =
			table[(c_Dimension - 1) * c_Dimension + c_Dimension - 1];  // rough, head-on

		CHECK(mirror.scale > 0.9f);
		CHECK(rough.scale < mirror.scale - 0.1f);
	}
}

// The laziness itself: the plan's acceptance says a scene with no PBR-lit material never builds
// the table. Drawing lit surfaces is not enough to trigger it -- only a bucket that shades
// through the engine's PBR is.
TEST_CASE("The BRDF LUT is generated only when PBR shading is drawn", "[brdflut][lit][render]")
{
	auto opts             = HeadlessOptions();
	opts.surfaceShaderDir = "./shaders/tests/surfaces";

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);
	const bgl::RenderContext* ctx = gfxBase->GetRenderContext();
	REQUIRE(ctx != nullptr);

	CHECK_FALSE(ctx->IsBrdfLutGenerated());

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 128;
	targetDesc.height   = 96;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 256;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 400000;
	sceneDesc.initialIndices              = 10000;
	sceneDesc.initialPbrMaterials         = 4;
	sceneDesc.initialSurfaceMaterials     = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	auto unlit = scene->CreateSurfaceMaterial({ .surface = "Unlit" });
	view->CreateStaticMeshInstance(scene->AddSphereGeom(16, 16, 3.0f, unlit), glm::mat4(1.0f));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = bgl::Camera()
	                   .LookAt(
						   glm::vec3(0.0f, 0.0f, 12.0f),
						   glm::vec3(0.0f, 0.0f, 11.0f),
						   glm::vec3(0.0f, 1.0f, 0.0f))
	                   .Perspective(glm::radians(60.0f), 128.0f / 96.0f, 0.5f, 100.0f);
	job.viewport = bgl::Viewport(128.0f, 96.0f);

	for (int i = 0; i < 3; ++i) gfx->DrawFrame(target, job);

	// Three frames of a lit surface drew, and none of them built the table.
	CHECK_FALSE(ctx->IsBrdfLutGenerated());

	auto pbr = scene->CreatePbrMaterial({});
	view->CreateStaticMeshInstance(
		scene->AddSphereGeom(16, 16, 3.0f, pbr),
		glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f)));

	gfx->DrawFrame(target, job);

	CHECK(ctx->IsBrdfLutGenerated());
}
