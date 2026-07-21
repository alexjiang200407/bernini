#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/idl.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "types/ComputeState.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <bgl/IGraphics.h>
#include <bgl/PsoType.h>

namespace
{
	// Matches TransparentDepthKeys.slang's [numthreads] (idl::cHistogramGroupSize).
	constexpr uint32_t c_ThreadsPerGroup = 256;

	struct SortEntry
	{
		uint32_t key;
		uint32_t instance;
	};
}

// The front half of the GPU transparent sort: pick the transparent instances out of the instance
// buffer, key each by camera distance, and compact them into (key, instance) pairs. The key is
// inverted so that an *ascending* sort emits farthest-first, which is the order blending needs --
// so a bug that keys near-first would silently reverse every blend once the sort lands.
TEST_CASE(
	"Transparent instances are keyed back-to-front and compacted",
	"[compute][transparentsort]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	// One mesh per placement, at a known distance down +Z from a camera at the origin. The
	// transparent ones are deliberately added nearest-first, so a sort that preserved insertion
	// order would fail the ordering check below.
	struct Placement
	{
		float        z;
		bgl::PsoType pso;
	};
	const std::array<Placement, 7> placements = { {
		{ 10.0f, bgl::PsoType::kTransparent_StaticMesh_PBR },
		{ 5.0f, bgl::PsoType::kOpaque_StaticMesh_PBR },
		{ 30.0f, bgl::PsoType::kTransparentOcclude_StaticMesh_LoosePbr },
		{ 20.0f, bgl::PsoType::kTransparent_StaticMesh_LoosePbr },
		{ 7.0f, bgl::PsoType::kAlphaTest_StaticMesh_PBR },
		{ 50.0f, bgl::PsoType::kTransparent_StaticMesh_PBR },
		// Exactly at the camera: distSq is 0, which inverts to all-ones and would key to the sort's
		// padding value if the key were not capped below it. Padding could then outrank a real entry
		// and put 0xFFFFFFFF into the drawn range, which ASBase dereferences unchecked.
		{ 0.0f, bgl::PsoType::kTransparent_StaticMesh_PBR },
	} };

	constexpr uint32_t c_PaddedCount = c_ThreadsPerGroup;

	auto meshBuffer = bgl::EntryBuffer<bgl::idl::Mesh>();
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = c_PaddedCount;
		desc.debugName = "Keys Mesh Buffer";
		meshBuffer.Init(std::move(desc), resourceManager);
	}

	auto instanceBuffer = bgl::PackedBuffer<bgl::SubmeshInstance>();
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = c_PaddedCount;
		desc.debugName = "Keys Instance Buffer";
		instanceBuffer.Init(desc, resourceManager);
	}

	// instanceIndex -> the z it was placed at, so the readback can be checked without assuming
	// which slot the GPU's atomic append handed each instance.
	std::unordered_map<uint32_t, float> zOfInstance;
	std::unordered_set<uint32_t>        transparentInstances;
	std::unordered_set<uint32_t>        occludeInstances;

	for (const Placement& placement : placements)
	{
		auto mesh      = bgl::idl::Mesh();
		mesh.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, placement.z));

		const auto meshHandle = meshBuffer.Add(mesh);

		auto instance         = bgl::SubmeshInstance();
		instance.meshInstance = meshHandle;
		instance.submeshIndex = 0;
		instance.pso          = static_cast<uint32_t>(placement.pso);

		const auto instanceHandle = instanceBuffer.Add(std::move(instance));
		const auto denseIndex     = static_cast<uint32_t>(zOfInstance.size());

		zOfInstance.emplace(denseIndex, placement.z);
		if (bgl::IsTransparentPso(static_cast<uint32_t>(placement.pso)))
		{
			transparentInstances.insert(denseIndex);
		}
		if (bgl::IsOccludeTransparentPso(static_cast<uint32_t>(placement.pso)))
		{
			occludeInstances.insert(denseIndex);
		}
		(void)instanceHandle;
	}

	auto entries = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<SortEntry>().SetMaxCount(c_PaddedCount).SetDebugName("Sort Entries");
		entries.Init(desc, resourceManager);
	}

	auto counter = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<uint32_t>().SetMaxCount(1).SetDebugName("Sort Entry Count");
		counter.Init(desc, resourceManager);
	}

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("TransparentDepthKeys"))
			.SetDebugName("Transparent Depth Keys"));

	kernel["gUniforms"]["instanceBuffer"] = instanceBuffer.GetBufferHandle();
	kernel["gUniforms"]["meshBuffer"]     = meshBuffer.GetBufferHandle();
	kernel["gUniforms"]["outEntries"]     = entries.GetBufferHandle();
	kernel["gUniforms"]["outCount"]       = counter.GetBufferHandle();
	kernel["gUniforms"]["cameraPos"]      = glm::vec3(0.0f);

	cmdList->Open(cmdQueue.Get(), cmdAllocator.Get());

	meshBuffer.Update(cmdList.Get());
	instanceBuffer.Update(cmdList.Get());
	entries.Clear(cmdList.Get());
	counter.Clear(cmdList.Get());

	// No FrameGraph here, so the copy -> compute transitions are ours to place: without them the
	// clear can land after the kernel's atomics and zero the count.
	const auto bufferBarrier = [](bgl::BarrierSyncFlag   syncBefore,
	                              bgl::BarrierAccessFlag accessBefore,
	                              bgl::BarrierSyncFlag   syncAfter,
	                              bgl::BarrierAccessFlag accessAfter) {
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(syncBefore)
		    .AddAccessBefore(accessBefore)
		    .AddSyncAfter(syncAfter)
		    .AddAccessAfter(accessAfter);
	};

	const auto toRead = bufferBarrier(
		bgl::BarrierSyncFlag::kCopy,
		bgl::BarrierAccessFlag::kCopyDest,
		bgl::BarrierSyncFlag::kComputeShader,
		bgl::BarrierAccessFlag::kShaderResource);
	const auto toWrite = bufferBarrier(
		bgl::BarrierSyncFlag::kCopy,
		bgl::BarrierAccessFlag::kCopyDest,
		bgl::BarrierSyncFlag::kComputeShader,
		bgl::BarrierAccessFlag::kUnorderedAccess);

	cmdList->Barrier(instanceBuffer.GetBufferHandle(), toRead);
	cmdList->Barrier(meshBuffer.GetBufferHandle(), toRead);
	cmdList->Barrier(entries.GetBufferHandle(), toWrite);
	cmdList->Barrier(counter.GetBufferHandle(), toWrite);

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	const auto toCopySource = bufferBarrier(
		bgl::BarrierSyncFlag::kComputeShader,
		bgl::BarrierAccessFlag::kUnorderedAccess,
		bgl::BarrierSyncFlag::kCopy,
		bgl::BarrierAccessFlag::kCopySource);

	cmdList->Barrier(entries.GetBufferHandle(), toCopySource);
	cmdList->Barrier(counter.GetBufferHandle(), toCopySource);

	const auto makeReadback = [&](uint64_t bytes, const char* name) {
		auto desc      = bgl::ReadbackBufferDesc();
		desc.byteSize  = bytes;
		desc.debugName = name;
		return resourceManager->CreateReadbackBuffer(desc);
	};
	auto entriesReadback = makeReadback(sizeof(SortEntry) * c_PaddedCount, "Sort Entries Readback");
	auto countReadback   = makeReadback(sizeof(uint32_t), "Sort Count Readback");

	cmdList->CopyBufferToReadback(entriesReadback, entries.GetBufferHandle());
	cmdList->CopyBufferToReadback(countReadback, counter.GetBufferHandle());

	cmdList->Close();
	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList.Get()));

	uint32_t count = 0;
	std::memcpy(&count, resourceManager->MapReadback(countReadback), sizeof(count));
	resourceManager->UnmapReadback(countReadback);

	// Only the transparent instances are keyed; the opaque and cutout ones draw from their PSO
	// bucket and must not appear here.
	REQUIRE(count == transparentInstances.size());

	std::vector<SortEntry> got(count);
	std::memcpy(
		got.data(),
		resourceManager->MapReadback(entriesReadback),
		sizeof(SortEntry) * count);
	resourceManager->UnmapReadback(entriesReadback);

	std::unordered_map<uint32_t, uint32_t> keyOfInstance;
	for (const SortEntry& entry : got)
	{
		INFO("instance " << entry.instance << " key " << entry.key);
		CHECK(transparentInstances.contains(entry.instance));

		// The sort reserves this value for its tail padding; a real key reaching it lets padding
		// displace a real entry into the drawn prefix.
		CHECK(entry.key != bgl::idl::cSortPadKey);

		keyOfInstance.emplace(entry.instance, entry.key);
	}
	REQUIRE(keyOfInstance.size() == transparentInstances.size());

	// Every self-occluding instance must key below every plain one, so one ascending sort leaves the
	// list already split into [occluders][the rest] -- the split the three fixed dispatches rely on.
	for (const auto& [lhs, lhsKey] : keyOfInstance)
	{
		for (const auto& [rhs, rhsKey] : keyOfInstance)
		{
			const bool lhsOccludes = occludeInstances.contains(lhs);
			const bool rhsOccludes = occludeInstances.contains(rhs);

			if (lhsOccludes && !rhsOccludes)
			{
				INFO("occluder " << lhs << " vs plain " << rhs);
				CHECK(lhsKey < rhsKey);
			}

			// Within a partition the contract is unchanged: farther away sorts first, so keys smaller.
			if (lhsOccludes == rhsOccludes && zOfInstance.at(lhs) > zOfInstance.at(rhs))
			{
				INFO("z " << zOfInstance.at(lhs) << " vs " << zOfInstance.at(rhs));
				CHECK(lhsKey < rhsKey);
			}
		}
	}

	resourceManager->DestroyReadbackBuffer(entriesReadback, 0, false);
	resourceManager->DestroyReadbackBuffer(countReadback, 0, false);
}
