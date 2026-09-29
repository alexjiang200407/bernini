#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/LodLevel.h>
#include <bgl_common/idl/InstanceLod.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>

// The word a placement's level of detail is kept in between frames, decoded on the CPU from the
// IDL's own shifts and masks. The words are written out literally, the way the Slang accessors
// would read them; a cull that writes one is the proof the two sides agree, and that is its test.

TEST_CASE("a zero word is a placement no cull has chosen for", "[lod][idl]")
{
	const bgl::InstanceLodState state = bgl::UnpackInstanceLod(bgl::idl::InstanceLod());

	CHECK_FALSE(state.level.has_value());
	CHECK_FALSE(state.outgoing.has_value());
	CHECK(state.fade == 1.0f);
}

TEST_CASE("a level rests in the low byte, one above its index", "[lod][idl]")
{
	const bgl::InstanceLodState state =
		bgl::UnpackInstanceLod(bgl::idl::InstanceLod{ .packed = 3u });

	REQUIRE(state.level.has_value());
	CHECK(*state.level == bgl::LodLevel::kLod2);
	CHECK_FALSE(state.outgoing.has_value());
	CHECK(state.fade == 1.0f);
}

TEST_CASE("a fading placement carries the outgoing level and its progress", "[lod][idl]")
{
	const uint32_t halfway = static_cast<uint32_t>(bgl::idl::cInstanceLodFadeScale / 2.0f);
	const uint32_t word    = 2u | (1u << bgl::idl::cInstanceLodOutgoingShift) |
	                         (halfway << bgl::idl::cInstanceLodFadeShift);

	const bgl::InstanceLodState state =
		bgl::UnpackInstanceLod(bgl::idl::InstanceLod{ .packed = word });

	REQUIRE(state.level.has_value());
	CHECK(*state.level == bgl::LodLevel::kLod1);
	REQUIRE(state.outgoing.has_value());
	CHECK(*state.outgoing == bgl::LodLevel::kLod0);
	CHECK(state.fade == Catch::Approx(0.5f).margin(1.0f / bgl::idl::cInstanceLodFadeScale));

	SECTION("a fade at its end is exactly one")
	{
		const uint32_t done = 2u | (1u << bgl::idl::cInstanceLodOutgoingShift) |
		                      (static_cast<uint32_t>(bgl::idl::cInstanceLodFadeScale)
		                       << bgl::idl::cInstanceLodFadeShift);
		CHECK(bgl::UnpackInstanceLod(bgl::idl::InstanceLod{ .packed = done }).fade == 1.0f);
	}
}

// The Slang side of the same word: InstanceLod::Make on the GPU, decoded on the CPU, and the
// geom's level-major entry lookup. This is the one place the two sides meet before a cull writes
// the word for real.
TEST_CASE(
	"the shader packs a word the CPU decodes, and finds a level's submesh",
	"[lod][idl][compute]")
{
	auto opts                             = bgl::test::GraphicsSetup();
	opts.context.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.context.enableDebugLayer         = true;
	opts.context.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
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

	constexpr uint32_t c_Words   = 5;
	constexpr uint32_t c_Entries = 5;

	auto wordsDesc         = bgl::ComputeBufferDesc();
	wordsDesc.initialCount = c_Words;
	wordsDesc.debugName    = "Lod Words";
	wordsDesc.SetElement<bgl::idl::InstanceLod>();
	const bgl::BufferHandle words = resourceManager->CreateComputeBuffer(wordsDesc);
	REQUIRE(resourceManager->ValidBufferHandle(words));

	auto entriesDesc         = bgl::ComputeBufferDesc();
	entriesDesc.initialCount = c_Entries;
	entriesDesc.debugName    = "Lod Entries";
	entriesDesc.SetElement<uint32_t>();
	const bgl::BufferHandle entries = resourceManager->CreateComputeBuffer(entriesDesc);
	REQUIRE(resourceManager->ValidBufferHandle(entries));

	auto rbWordsDesc                        = bgl::ReadbackBufferDesc();
	rbWordsDesc.byteSize                    = c_Words * sizeof(bgl::idl::InstanceLod);
	rbWordsDesc.debugName                   = "Lod Words Readback";
	const bgl::ReadbackBufferHandle rbWords = resourceManager->CreateReadbackBuffer(rbWordsDesc);

	auto rbEntriesDesc      = bgl::ReadbackBufferDesc();
	rbEntriesDesc.byteSize  = c_Entries * sizeof(uint32_t);
	rbEntriesDesc.debugName = "Lod Entries Readback";
	const bgl::ReadbackBufferHandle rbEntries =
		resourceManager->CreateReadbackBuffer(rbEntriesDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSLodWord"))
			.SetDebugName("Lod Word"));
	REQUIRE(kernel.pipeline != nullptr);

	kernel["gUniforms"]["words"]   = words;
	kernel["gUniforms"]["entries"] = entries;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	cmdList->Open(cmdQueue, cmdAllocator);
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	for (const bgl::BufferHandle buffer : { words, entries })
	{
		cmdList->Barrier(
			buffer,
			bgl::BufferBarrierDesc()
				.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));
	}

	cmdList->CopyBufferToReadback(rbWords, words);
	cmdList->CopyBufferToReadback(rbEntries, entries);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* packed =
		static_cast<const bgl::idl::InstanceLod*>(resourceManager->MapReadback(rbWords));
	REQUIRE(packed != nullptr);

	// What the shader packed, in the order CSLodWord.slang writes them.
	struct Expected
	{
		bgl::LodLevel                level;
		std::optional<bgl::LodLevel> outgoing;
		float                        fade;
	};
	using enum bgl::LodLevel;
	const std::array<Expected, c_Words> expected = { {
		{ kLod0, std::nullopt, 1.0f },
		{ kLod2,
		  std::nullopt,
		  1.0f },  // not fading: the outgoing level and fade it was handed are dropped
		{ kLod1, kLod0, 0.5f },
		{ kLod3, kLod1, 0.0f },
		{ kLod7, kLod6, 1.0f },
	} };
	for (uint32_t i = 0; i < c_Words; ++i)
	{
		CAPTURE(i, packed[i].packed);
		const bgl::InstanceLodState read = bgl::UnpackInstanceLod(packed[i]);
		REQUIRE(read.level.has_value());
		CHECK(*read.level == expected[i].level);
		CHECK(read.outgoing == expected[i].outgoing);
		CHECK(
			read.fade ==
			Catch::Approx(expected[i].fade).margin(1.0f / bgl::idl::cInstanceLodFadeScale));
	}
	resourceManager->UnmapReadback(rbWords);

	const auto* entry = static_cast<const uint32_t*>(resourceManager->MapReadback(rbEntries));
	REQUIRE(entry != nullptr);

	// Three submeshes a level, two levels: level 1's submesh s is entry 3 + s.
	CHECK(entry[0] == 0u);
	CHECK(entry[1] == 2u);
	CHECK(entry[2] == 3u);
	CHECK(entry[3] == 5u);
	CHECK(entry[4] == 6u);
	resourceManager->UnmapReadback(rbEntries);

	resourceManager->DestroyReadbackBuffer(rbEntries, false);
	resourceManager->DestroyReadbackBuffer(rbWords, false);
	resourceManager->DestroyBuffer(entries, false);
	resourceManager->DestroyBuffer(words, false);
}
