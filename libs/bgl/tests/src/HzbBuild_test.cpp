#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/GraphicsBase.h"
#include "gfx/frame_constants.h"
#include "passes/DrawData.h"
#include "passes/HzbBuildPass.h"
#include "passes/PassInitContext.h"
#include "scene/CullState.h"
#include "scene/HzbChain.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TextureReadback.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/ISceneView.h>
#include <bgl/idl/Constants.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// The HZB ladder built from a depth of known values: every level holds the farthest depth of the
// texels it covers, the sizes round up so the last texel of an odd axis keeps its one real source
// rather than reading past it, and the top level is the farthest texel of the whole depth.

namespace
{
	// Odd on both axes, and at every level but the last two (7x5 -> 4x3 -> 2x2 -> 1x1).
	constexpr uint32_t c_Width  = 7;
	constexpr uint32_t c_Height = 5;

	// A depth every texel of which differs: nearest (largest, under reversed-Z) at the top-left,
	// stepping down to the right and down, with the odd last column and row the farthest of all so
	// a reduce that dropped them would read nearer than it should.
	float
	DepthAt(const uint32_t x, const uint32_t y)
	{
		return 0.9f - 0.01f * static_cast<float>(x) - 0.1f * static_cast<float>(y);
	}

	struct Grid
	{
		uint32_t           width  = 0;
		uint32_t           height = 0;
		std::vector<float> texels;

		[[nodiscard]] float
		At(const uint32_t x, const uint32_t y) const
		{
			return texels[static_cast<size_t>(y) * width + x];
		}
	};

	// The reduce the pass is specified to do: half the source rounded up, each texel the minimum
	// of its 2x2, clamped at the source's edge.
	Grid
	Reduce(const Grid& source)
	{
		auto grid   = Grid();
		grid.width  = (source.width + 1u) / 2u;
		grid.height = (source.height + 1u) / 2u;
		grid.texels.resize(static_cast<size_t>(grid.width) * grid.height);
		for (uint32_t y = 0; y < grid.height; ++y)
		{
			for (uint32_t x = 0; x < grid.width; ++x)
			{
				float farthest = 1.0f;
				for (uint32_t dy = 0; dy < 2u; ++dy)
				{
					for (uint32_t dx = 0; dx < 2u; ++dx)
					{
						const uint32_t sx = std::min(x * 2u + dx, source.width - 1u);
						const uint32_t sy = std::min(y * 2u + dy, source.height - 1u);
						farthest          = std::min(farthest, source.At(sx, sy));
					}
				}
				grid.texels[static_cast<size_t>(y) * grid.width + x] = farthest;
			}
		}
		return grid;
	}
}

TEST_CASE("The HZB holds the farthest depth under every texel of every level", "[culling][hzb]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	auto device          = gfxBase->GetDevice();

	// The depth, as a float texture the pass reads through the draw's depth SRV.
	auto depth   = Grid();
	depth.width  = c_Width;
	depth.height = c_Height;
	for (uint32_t y = 0; y < c_Height; ++y)
	{
		for (uint32_t x = 0; x < c_Width; ++x)
		{
			depth.texels.push_back(DepthAt(x, y));
		}
	}

	auto texDesc            = bgpu::TextureDesc();
	texDesc.width           = c_Width;
	texDesc.height          = c_Height;
	texDesc.format          = bgpu::Format::R32_FLOAT;
	texDesc.usage           = bgpu::TextureUsageFlag::kSRV;
	texDesc.initialLayout   = bgpu::BarrierLayout::kCopyDest;
	texDesc.debugName       = "HZB Test Depth";
	const auto depthTexture = resourceManager->CreateTexture(texDesc);
	REQUIRE(resourceManager->ValidTextureHandle(depthTexture));

	auto srvDesc        = bgpu::SrvDesc();
	srvDesc.format      = texDesc.format;
	srvDesc.debugName   = "HZB Test Depth SRV";
	const auto depthSrv = resourceManager->CreateSrv(depthTexture, srvDesc);
	REQUIRE(resourceManager->ValidSrvHandle(depthSrv));

	auto cmdListDesc  = bgpu::CommandListDesc();
	cmdListDesc.type  = bgpu::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	{
		auto sub       = bgpu::TextureSubresourceData();
		sub.data       = depth.texels.data();
		sub.rowPitch   = c_Width * sizeof(float);
		sub.slicePitch = sub.rowPitch * c_Height;
		const std::array<bgpu::TextureSubresourceData, 1> subresources{ sub };

		cmdList->Open(cmdQueue, cmdAllocator);
		cmdList->WriteTexture(depthTexture, subresources);
		cmdList->Barrier(
			depthTexture,
			bgpu::TextureBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
				.SetLayoutBefore(bgpu::BarrierLayout::kCopyDest)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kPixelShader)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource)
				.SetLayoutAfter(bgpu::BarrierLayout::kShaderResource));
		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));
	}

	auto cullState = bgl::CullState(resourceManager, bgl::idl::cHistogramGroupSize, 1);
	CHECK(cullState.GetHzb().Ensure(resourceManager, c_Width, c_Height));
	CHECK_FALSE(cullState.GetHzb().IsValid());

	const std::span<const bgl::HzbChain::Level> levels = cullState.GetHzb().GetLevels();
	REQUIRE(levels.size() == 3);
	CHECK(levels[0].width == 4);
	CHECK(levels[0].height == 3);
	CHECK(levels[1].width == 2);
	CHECK(levels[1].height == 2);
	CHECK(levels[2].width == 1);
	CHECK(levels[2].height == 1);

	// The same size again is a no-op; another size remakes the ladder.
	CHECK_FALSE(cullState.GetHzb().Ensure(resourceManager, c_Width, c_Height));

	auto       pipelines = bgpu::PipelineBatch(device);
	const auto ctx       = bgl::PassInitContext{ device, &pipelines, resourceManager, nullptr };
	auto       pass      = bgl::HzbBuildPass(ctx);
	pipelines.Build();
	pass.CheckBindings();

	bgl::FrameGraph fg;
	fg.RegisterQueue("main", cmdQueue, cmdList);
	fg.ImportTexture(
		bgl::c_DepthName,
		depthTexture,
		bgl::AccessState{ bgpu::BarrierSyncFlag::kPixelShader,
	                      bgpu::BarrierAccessFlag::kShaderResource,
	                      bgpu::BarrierLayout::kShaderResource });

	auto updates = std::vector<std::string>();
	cullState.ImportResources(fg, "c0:", updates);

	auto draw             = bgl::DrawData();
	draw.cullState        = &cullState;
	draw.targets.depthSrv = depthSrv;
	pass.AttachToFrameGraph(fg, draw, "Test");
	fg.Compile(resourceManager.Get());

	cmdAllocator->ResetAllocator();
	cmdList->Open(cmdQueue, cmdAllocator);
	fg.Execute();
	cmdList->Close();
	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	Grid expected = depth;
	for (uint32_t i = 0; i < levels.size(); ++i)
	{
		expected = Reduce(expected);
		REQUIRE(expected.width == levels[i].width);
		REQUIRE(expected.height == levels[i].height);

		const std::vector<uint8_t> bytes = bgl::test::ReadTextureBytes(
			gfx.Get(),
			levels[i].texture,
			levels[i].width,
			levels[i].height,
			sizeof(float),
			// The graph leaves a level the next one read as a shader resource, and the last as
			// the render target it was drawn into.
			i + 1 == levels.size() ? bgpu::BarrierLayout::kRenderTarget :
									 bgpu::BarrierLayout::kShaderResource);
		REQUIRE(bytes.size() == expected.texels.size() * sizeof(float));

		auto got = std::vector<float>(expected.texels.size());
		std::memcpy(got.data(), bytes.data(), bytes.size());

		for (uint32_t y = 0; y < expected.height; ++y)
		{
			for (uint32_t x = 0; x < expected.width; ++x)
			{
				CAPTURE(i, x, y);
				CHECK(got[static_cast<size_t>(y) * expected.width + x] == expected.At(x, y));
			}
		}
	}

	// The top level is the farthest texel of the whole depth: the odd last column and row's corner.
	CHECK(expected.At(0, 0) == DepthAt(c_Width - 1u, c_Height - 1u));

	resourceManager->DestroySrv(depthSrv);
	resourceManager->DestroyTexture(depthTexture);
}
