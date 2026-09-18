#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <vector>

/**
 * The screen rect lib.math.box_bounds gives a box, read back from the GPU.
 *
 * It decides whether a blob-shadow decal draws a quad at all and how much of the screen that quad
 * covers -- which no image can see, since a rect too large shades exactly the same pixels as a
 * tight one and only costs more. So the rect itself is asserted, against projections worked out
 * here by hand.
 *
 * The camera sits at (0, 1.5, 0) looking down -Z, so a point's view space is its world position
 * less 1.5 in y, and a point on the near plane projects to x * P[0][0] / near.
 */
namespace
{
	constexpr float c_Near   = 0.5f;
	constexpr float c_Far    = 100.0f;
	constexpr float c_Aspect = 4.0f / 3.0f;
	constexpr float c_EyeY   = 1.5f;

	constexpr uint32_t c_Corners = 8;

	struct Box
	{
		glm::vec3 lo;
		glm::vec3 hi;
	};

	// Bit k of a corner's index picks the high side of axis k, the order the module reads edges in.
	std::array<glm::vec3, c_Corners>
	Corners(const Box& box)
	{
		auto corners = std::array<glm::vec3, c_Corners>();
		for (uint32_t i = 0; i < c_Corners; ++i)
		{
			corners[i] = glm::vec3(
				(i & 1u) != 0 ? box.hi.x : box.lo.x,
				(i & 2u) != 0 ? box.hi.y : box.lo.y,
				(i & 4u) != 0 ? box.hi.z : box.lo.z);
		}
		return corners;
	}

	/** (lo.x, lo.y, hi.x, hi.y) per box, all zeros for a box that emits nothing. */
	std::vector<glm::vec4>
	ProjectOnGpu(const glm::mat4& viewProj, const std::vector<Box>& boxes)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto* gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto  resourceManager = gfxBase->GetResourceManagerCpy();
		auto* device          = gfxBase->GetDevice();

		auto cmdListDesc  = bgl::CommandListDesc();
		cmdListDesc.type  = bgl::QueueType::kGraphics;
		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		const auto count = static_cast<uint32_t>(boxes.size());

		auto corners = std::vector<glm::vec4>();
		for (const Box& box : boxes)
		{
			for (const glm::vec3& corner : Corners(box))
			{
				corners.emplace_back(corner, 1.0f);
			}
		}

		auto cornerDesc         = bgl::ComputeBufferDesc();
		cornerDesc.initialCount = count * c_Corners;
		cornerDesc.debugName    = "Box Corners";
		cornerDesc.SetElement<glm::vec4>();
		const bgl::BufferHandle cornerBuffer = resourceManager->CreateComputeBuffer(cornerDesc);
		REQUIRE(resourceManager->ValidBufferHandle(cornerBuffer));

		auto rectDesc         = bgl::ComputeBufferDesc();
		rectDesc.initialCount = count;
		rectDesc.debugName    = "Box Rects";
		rectDesc.SetElement<glm::vec4>();
		const bgl::BufferHandle rectBuffer = resourceManager->CreateComputeBuffer(rectDesc);
		REQUIRE(resourceManager->ValidBufferHandle(rectBuffer));

		auto rbDesc                        = bgl::ReadbackBufferDesc();
		rbDesc.byteSize                    = count * sizeof(glm::vec4);
		rbDesc.debugName                   = "Box Rects Readback";
		const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSBoxBounds"))
				.SetDebugName("Box Bounds"));
		REQUIRE(kernel.pipeline != nullptr);

		kernel["gUniforms"]["corners"]  = cornerBuffer;
		kernel["gUniforms"]["rects"]    = rectBuffer;
		kernel["gUniforms"]["viewProj"] = viewProj;
		kernel["gUniforms"]["boxCount"] = count;

		cmdList->Open(cmdQueue, cmdAllocator);

		cmdList->WriteBuffer(cornerBuffer, corners.data(), 0, corners.size() * sizeof(glm::vec4));
		cmdList->Barrier(
			cornerBuffer,
			bgl::BufferBarrierDesc()
				.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
				.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
				.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
				.AddAccessAfter(bgl::BarrierAccessFlag::kUnorderedAccess));

		auto computeState   = bgl::ComputeState();
		computeState.kernel = &kernel;
		cmdList->SetComputeState(computeState);
		cmdList->Dispatch(count, 1, 1);

		cmdList->Barrier(
			rectBuffer,
			bgl::BufferBarrierDesc()
				.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

		cmdList->CopyBufferToReadback(rb, rectBuffer);
		cmdList->Close();

		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* got = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
		REQUIRE(got != nullptr);
		return std::vector<glm::vec4>(got, got + count);
	}
}

TEST_CASE(
	"A box's screen rect is clipped to the near plane and empty off-frustum",
	"[boxbounds][compute]")
{
	const glm::mat4 proj = glm::perspective(glm::radians(60.0f), c_Aspect, c_Near, c_Far);
	const glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, c_EyeY, 0.0f),
		glm::vec3(0.0f, c_EyeY, -1.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 viewProj = proj * view;

	const float px = proj[0][0];
	const float py = proj[1][1];

	// Every corner in front of the near plane: the rect is exactly the corners' projected bounds.
	const Box inFront{ { -1.0f, 0.0f, -8.0f }, { 1.0f, 1.0f, -6.0f } };

	// A sliver right of centre and level with the eye, running from 2 m ahead to 2 m behind. Its
	// right and vertical extents come from where its edges cross the near plane, which no corner
	// sits on; its left edge is its far corner.
	const Box straddling{ { 0.2f, c_EyeY - 0.1f, -2.0f }, { 0.3f, c_EyeY + 0.1f, 2.0f } };

	// A blob shadow's volume under the camera, reaching behind it: the lower screen only.
	const Box underfoot{ { -2.0f, 0.0f, -4.0f }, { 2.0f, 0.5f, 4.0f } };

	const std::vector<Box> boxes{
		{ { -1.0f, 0.0f, 3.0f }, { 1.0f, 1.0f, 5.0f } },         // behind the camera
		{ { -1.0f, 0.0f, -150.0f }, { 1.0f, 1.0f, -120.0f } },   // beyond the far plane
		{ { -40.0f, 0.0f, -12.0f }, { -30.0f, 1.0f, -10.0f } },  // left of the frustum
		{ { -1.0f, 30.0f, -12.0f }, { 1.0f, 40.0f, -10.0f } },   // above the frustum
		inFront,
		straddling,
		underfoot,
		{ { -1.0f, 1.0f, -1.0f }, { 1.0f, 2.0f, 1.0f } },  // the camera inside it
	};

	const std::vector<glm::vec4> rects = ProjectOnGpu(viewProj, boxes);
	REQUIRE(rects.size() == boxes.size());

	constexpr float c_Margin = 1e-4f;

	const auto requireRect = [&](const glm::vec4& got, const glm::vec4& expected) {
		CHECK(got.x == Catch::Approx(expected.x).margin(c_Margin));
		CHECK(got.y == Catch::Approx(expected.y).margin(c_Margin));
		CHECK(got.z == Catch::Approx(expected.z).margin(c_Margin));
		CHECK(got.w == Catch::Approx(expected.w).margin(c_Margin));
	};

	SECTION("a box the frustum does not reach emits nothing")
	{
		CHECK(rects[0] == glm::vec4(0.0f));
		CHECK(rects[1] == glm::vec4(0.0f));
		CHECK(rects[2] == glm::vec4(0.0f));
		CHECK(rects[3] == glm::vec4(0.0f));
	}

	SECTION("a box wholly in front is bounded by its projected corners")
	{
		auto lo = glm::vec2(1.0f);
		auto hi = glm::vec2(-1.0f);
		for (const glm::vec3& corner : Corners(inFront))
		{
			const glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
			const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
			lo                   = glm::min(lo, ndc);
			hi                   = glm::max(hi, ndc);
		}
		requireRect(rects[4], glm::vec4(lo, hi));
	}

	SECTION("a box through the near plane is bounded where its edges cross it")
	{
		const float halfHeight = straddling.hi.y - c_EyeY;
		requireRect(
			rects[5],
			glm::vec4(
				straddling.lo.x * px / -straddling.lo.z,
				-halfHeight * py / c_Near,
				straddling.hi.x * px / c_Near,
				halfHeight * py / c_Near));
	}

	SECTION("a volume under the camera covers only the screen below its far top edge")
	{
		const float top = (underfoot.hi.y - c_EyeY) * py / -underfoot.lo.z;
		REQUIRE(top < 0.0f);
		requireRect(rects[6], glm::vec4(-1.0f, -1.0f, 1.0f, top));
	}

	SECTION("a box around the camera covers the whole viewport")
	{
		requireRect(rects[7], glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f));
	}
}
