#pragma once
// idl::SkinnedState arrived through the idl.h umbrella this sweep removed.
#include "gfx/GraphicsBase.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include <bgl/idl/Constants.h>
#include <bgl/idl/MeshInstance.h>
#include <bgl/idl/SkinnedState.h>
#include <bgl/types/MeshInstanceHandle.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

// Reading a pose back off the GPU, shared by every suite that checks one. A golden image can say a
// skinned pose is wrong; only the palette itself can say which stage got it wrong.

namespace bgl::test
{
	/**
 * The palette as the pose pass left it: `cFloat4sPerBone` rows a bone, each row of a skinning
 * matrix as the shader indexed it, so `dot(row[i], float4(p, 1))` is component i of `p` skinned.
 */
	struct Palette
	{
		std::vector<glm::vec4> rows;

		[[nodiscard]] glm::vec3
		Apply(uint32_t bone, const glm::vec3& point) const
		{
			const glm::vec4 p(point, 1.0f);
			const size_t    base = size_t(bone) * bgl::idl::cFloat4sPerBone;
			return { glm::dot(rows[base + 0], p),
				     glm::dot(rows[base + 1], p),
				     glm::dot(rows[base + 2], p) };
		}
	};

	/** Copies `float4Count` float4s out of the view's palette arena, starting at `base`. */
	inline Palette
	ReadPalette(
		bgl::GraphicsBase*    gfxBase,
		const bgl::SceneView* view,
		uint32_t              base,
		uint32_t              float4Count)
	{
		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();

		auto cmdListDesc = bgpu::CommandListDesc();
		cmdListDesc.type = bgpu::QueueType::kGraphics;

		// Drain the renderer first: this copy rides its own queue, which nothing orders against the
		// frame that wrote the palette.
		gfxBase->WaitIdle();

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		cmdAllocator->ResetAllocator();

		const bgpu::BufferHandle palettes = view->GetPalettes().GetBufferHandle();

		// The whole arena, not just the slice wanted: CopyBufferToReadback copies the entire source
		// buffer, so a destination sized to the slice overruns it -- silently, until GPU validation
		// is on.
		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = uint64_t(view->GetPalettes().Capacity()) * sizeof(glm::vec4);
		rbDesc.debugName = "Bone Palette Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);

		// The pass left it in UAV state; a copy needs it as a source.
		auto barrier = bgpu::BufferBarrierDesc();
		barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(palettes, barrier);

		cmdList->CopyBufferToReadback(rb, palettes);
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);

		auto palette = Palette();
		palette.rows.assign(mapped + base, mapped + base + float4Count);

		resourceManager->UnmapReadback(rb);
		return palette;
	}

	/** Where `instance`'s palette begins. Never assume 0: the arena reserves element 0 as its null. */
	inline uint32_t
	PaletteBaseOf(bgl::SceneView* view, bgl::MeshInstanceHandle instance)
	{
		auto& meshBuffer = view->GetMeshBuffer();
		auto& playback   = view->GetPlaybackArena();

		const bgl::idl::MeshInstance& mesh = meshBuffer.AtIndex(instance.handle.index);
		REQUIRE_FALSE(mesh.playback.Null());

		return playback.GetPayloadAt<bgl::idl::SkinnedState>(mesh.playback.byteOffset)
		    .palette.offsetStart;
	}

	inline void
	CheckNear(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == Catch::Approx(expected.x).margin(1e-4));
		CHECK(actual.y == Catch::Approx(expected.y).margin(1e-4));
		CHECK(actual.z == Catch::Approx(expected.z).margin(1e-4));
	}

	/**
	 * The first `count` elements of a GPU-written buffer, as `T`. Waits for the device first: the
	 * copy rides its own queue, which nothing orders against the frame that wrote the buffer.
	 */
	template <typename T>
	std::vector<T>
	ReadBuffer(bgl::GraphicsBase* gfxBase, bgpu::BufferHandle buffer, uint32_t count)
	{
		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();

		gfxBase->WaitIdle();

		auto cmdListDesc = bgpu::CommandListDesc();
		cmdListDesc.type = bgpu::QueueType::kGraphics;

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		cmdAllocator->ResetAllocator();

		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = uint64_t(count) * sizeof(T);
		rbDesc.debugName = "Test Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);

		auto barrier = bgpu::BufferBarrierDesc();
		barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(buffer, barrier);

		cmdList->CopyBufferToReadback(rb, buffer);
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const T*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);

		auto rows = std::vector<T>(mapped, mapped + count);

		resourceManager->UnmapReadback(rb);
		resourceManager->DestroyReadbackBuffer(rb, false);
		return rows;
	}

	/** The scene's whole bone anim table arena, table soles included, as float4 rows. */
	inline std::vector<glm::vec4>
	ReadBoneAnimTables(bgl::GraphicsBase* gfxBase, bgl::Scene* scene)
	{
		return ReadBuffer<glm::vec4>(
			gfxBase,
			scene->GetBoneAnimTables().GetBufferHandle(),
			scene->GetBoneAnimTables().Capacity());
	}
}
