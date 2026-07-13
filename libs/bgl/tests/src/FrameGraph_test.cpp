#include "cmd/CommandQueue.h"
#include "fg/FrameGraph.h"
#include "resource/ResourceManager.h"

#include <core/ref/RefCounter.h>

using namespace bgl;

namespace
{
	BufferHandle
	MakeBuffer(uint32_t idx)
	{
		return BufferHandle({ { idx, 0 } });
	}

	BufferArg
	UavBuf(std::string name)
	{
		return { std::move(name),
			     BarrierSyncFlag::kComputeShader,
			     BarrierAccessFlag::kUnorderedAccess };
	}

	BufferArg
	SrvBuf(std::string name)
	{
		return { std::move(name),
			     BarrierSyncFlag::kPixelShader,
			     BarrierAccessFlag::kShaderResource };
	}

	TextureArg
	SrvTex(std::string name)
	{
		return { std::move(name),
			     BarrierSyncFlag::kPixelShader,
			     BarrierAccessFlag::kShaderResource,
			     BarrierLayout::kShaderResource };
	}

	// A do-nothing ICommandList so Execute() can run without a GPU device.
	class NullCommandList : public core::RefCounter<ICommandList>
	{
	public:
		NullCommandList()                       = default;
		NullCommandList(const NullCommandList&) = delete;
		NullCommandList(NullCommandList&&)      = delete;

		NullCommandList&
		operator=(const NullCommandList&) = delete;

		NullCommandList&
		operator=(NullCommandList&&) = delete;

		void
		WriteBuffer(BufferHandle, const void*, size_t, size_t) noexcept override
		{}
		void
		WriteTexture(TextureHandle, std::span<const TextureSubresourceData>) noexcept override
		{}
		void
		CopyBufferToReadback(ReadbackBufferHandle, BufferHandle) noexcept override
		{}
		void
		CopyTextureToReadback(ReadbackBufferHandle, TextureHandle) noexcept override
		{}
		void
		Barrier(BufferHandle, const BufferBarrierDesc&) noexcept override
		{}
		void
		Barrier(TextureHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(RtvHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(DsvHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(std::span<const BufferHandle>, std::span<const BufferBarrierDesc>) noexcept override
		{}
		void
		Barrier(std::span<const TextureHandle>, std::span<const TextureBarrierDesc>) noexcept
			override
		{}
		void
		Open(ICommandQueue*, ICommandAllocator*) noexcept override
		{}
		void
		Close() noexcept override
		{}
		void
		BeginEvent(std::string_view) noexcept override
		{}
		void
		EndEvent() noexcept override
		{}
		void
		SetMeshletState(const MeshletState&) noexcept override
		{}
		void
		SetComputeState(const ComputeState&) noexcept override
		{}
		void
		DispatchMesh(uint32_t, uint32_t, uint32_t) noexcept override
		{}
		void
		DispatchMeshIndirect(uint32_t) noexcept override
		{}
		void
		Dispatch(uint32_t, uint32_t, uint32_t) noexcept override
		{}
		bool
		IsOpen() const noexcept override
		{
			return false;
		}
		QueueType
		GetType() const noexcept override
		{
			return QueueType{};
		}
	};

	// A do-nothing ICommandQueue, paired with NullCommandList for Execute() tests.
	class NullCommandQueue : public core::RefCounter<ICommandQueue>
	{
	public:
		NullCommandQueue()                        = default;
		NullCommandQueue(const NullCommandQueue&) = delete;
		NullCommandQueue(NullCommandQueue&&)      = delete;

		NullCommandQueue&
		operator=(const NullCommandQueue&) = delete;

		NullCommandQueue&
		operator=(NullCommandQueue&&) = delete;

		uint64_t
		ExecuteCommandList(ICommandList*) noexcept override
		{
			return 0;
		}
		bool
		IsFenceComplete(uint64_t) noexcept override
		{
			return true;
		}
		uint64_t
		PollCurrentFenceValue() noexcept override
		{
			return 0;
		}
		uint64_t
		GetLastCompletedFence() const noexcept override
		{
			return 0;
		}
		uint64_t
		GetNextFenceValue() const noexcept override
		{
			return 0;
		}
		void
		InsertWait(uint64_t) noexcept override
		{}
		void
		InsertWaitForQueueFence(ICommandQueue*, uint64_t) const noexcept override
		{}
		void
		InsertWaitForQueue(ICommandQueue*) const noexcept override
		{}
		void
		WaitForFenceCPUBlocking(uint64_t) noexcept override
		{}
	};

	// A ResourceManager that only resolves attachment views to textures; the rest
	// of the interface is unused by the frame graph and aborts if ever called.
	class MockResourceManager : public core::RefCounter<IResourceManager>
	{
	public:
		MockResourceManager()                           = default;
		MockResourceManager(const MockResourceManager&) = delete;
		MockResourceManager(MockResourceManager&&)      = delete;

		MockResourceManager&
		operator=(const MockResourceManager&) = delete;

		MockResourceManager&
		operator=(MockResourceManager&&) = delete;

		std::unordered_map<uint32_t, TextureHandle> rtvToTexture;
		std::unordered_map<uint32_t, TextureHandle> dsvToTexture;

		TextureHandle
		GetRtvTexture(RtvHandle handle) const noexcept override
		{
			const auto it = rtvToTexture.find(handle.idx);
			return it != rtvToTexture.end() ? it->second : TextureHandle{};
		}
		TextureHandle
		GetDsvTexture(DsvHandle handle) const noexcept override
		{
			const auto it = dsvToTexture.find(handle.idx);
			return it != dsvToTexture.end() ? it->second : TextureHandle{};
		}

		BufferHandle
		CreateStructBuffer(const StructBufferDesc&) noexcept override
		{
			return {};
		}
		BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc&) noexcept override
		{
			return {};
		}
		TextureHandle
		CreateTexture(const TextureDesc&) noexcept override
		{
			return {};
		}
		TextureHandle
		CreateTexture(const TextureDesc&, std::span<const TextureSubresourceData>) noexcept override
		{
			return {};
		}
		TextureHandle
		CreateTexture(const assetlib::ImageData&, std::string) noexcept override
		{
			return {};
		}
		SamplerHandle
		CreateSampler(const SamplerDesc&) noexcept override
		{
			return {};
		}
		TextureHandle
		CreateSolidTexture(uint8_t, uint8_t, uint8_t, uint8_t) noexcept override
		{
			return {};
		}
		void
		FlushPendingTextureUploads(ICommandList*) noexcept override
		{}
		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc&) noexcept override
		{
			return {};
		}
		void
		DestroyBuffer(BufferHandle, uint64_t, bool) noexcept override
		{}
		void
		DestroyTexture(TextureHandle, uint64_t, bool) noexcept override
		{}
		void
		DestroyTexture(TextureHandle) noexcept override
		{}
		void
		DestroySampler(SamplerHandle, uint64_t, bool) noexcept override
		{}
		void
		DestroyReadbackBuffer(ReadbackBufferHandle, uint64_t, bool) noexcept override
		{}
		void
		DestroyRtv(RtvHandle, uint64_t, bool) noexcept override
		{}
		void
		DestroyDsv(DsvHandle, uint64_t, bool) noexcept override
		{}
		void
		CleanupExpiredResources(uint64_t) noexcept override
		{}
		RtvHandle
		CreateRtv(TextureHandle, const RtvDesc&) noexcept override
		{
			return {};
		}
		DsvHandle
		CreateDsv(TextureHandle, const DsvDesc&) noexcept override
		{
			return {};
		}
		const Rtv&
		GetRtv(RtvHandle) const noexcept override
		{
			std::abort();
		}
		const Dsv&
		GetDsv(DsvHandle) const noexcept override
		{
			std::abort();
		}
		const Buffer&
		GetBuffer(BufferHandle) const noexcept override
		{
			std::abort();
		}
		const Texture&
		GetTexture(TextureHandle) const noexcept override
		{
			std::abort();
		}

		const Sampler&
		GetSampler(SamplerHandle) const noexcept override
		{
			std::abort();
		}

		const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle) const noexcept override
		{
			std::abort();
		}
		TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle) const noexcept override
		{
			return {};
		}
		const void*
		MapReadback(ReadbackBufferHandle) noexcept override
		{
			return nullptr;
		}
		void
		UnmapReadback(ReadbackBufferHandle) noexcept override
		{}
		bool
		ValidBufferHandle(const BufferHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidTextureHandle(const TextureHandle&) const noexcept override
		{
			return false;
		}
		bool
		IsTextureCube(const TextureHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidSamplerHandle(const SamplerHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidRtvHandle(const RtvHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidDsvHandle(const DsvHandle&) const noexcept override
		{
			return false;
		}
		void
		ClearRtv(ICommandList*, RtvHandle, float[4]) noexcept override
		{}
		void
		ClearDsv(ICommandList*, DsvHandle, float, uint8_t) noexcept override
		{}
	};

	// Compile() requires a ResourceManager; tests without attachments use this
	// shared empty mock (its resolve maps are never consulted).
	MockResourceManager&
	NullRm()
	{
		static MockResourceManager rm;
		return rm;
	}
}

TEST_CASE("FrameGraph: culls a pass whose outputs are never used", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("backbuffer", MakeBuffer(1));

	fg.AddPass(
		PassDesc{}.SetName("Main").AddBufferArg(UavBuf("backbuffer")));  // writes imported -> root
	fg.AddPass(
		PassDesc{}.SetName("Unused").AddBufferArg(UavBuf("scratch")));  // transient, never read

	fg.Compile(&NullRm());

	CHECK(fg.ExecutionOrder() == std::vector<std::string>{ "Main" });
	CHECK(fg.WasCulled("Unused"));
	CHECK_FALSE(fg.WasCulled("Main"));
}

TEST_CASE("FrameGraph: a consumed producer survives culling", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("backbuffer", MakeBuffer(1));

	fg.AddPass(PassDesc{}.SetName("Produce").AddBufferArg(UavBuf("gbuffer")));  // transient write
	fg.AddPass(
		PassDesc{}
			.SetName("Consume")
			.AddBufferArg(SrvBuf("gbuffer"))       // reads the producer's output
			.AddBufferArg(UavBuf("backbuffer")));  // writes imported -> root

	fg.Compile(&NullRm());

	CHECK(fg.ExecutionOrder() == std::vector<std::string>{ "Produce", "Consume" });
}

TEST_CASE("FrameGraph: transitively culls a dead producer chain", "[fg]")
{
	FrameGraph fg;

	fg.AddPass(PassDesc{}.SetName("A").AddBufferArg(UavBuf("t1")));
	fg.AddPass(PassDesc{}.SetName("B").AddBufferArg(SrvBuf("t1")).AddBufferArg(UavBuf("t2")));

	fg.Compile(&NullRm());

	CHECK(fg.ExecutionOrder().empty());
	CHECK(fg.WasCulled("A"));
	CHECK(fg.WasCulled("B"));
}

TEST_CASE("FrameGraph: SetSideEffect pins an otherwise-dead pass", "[fg]")
{
	FrameGraph fg;
	fg.AddPass(PassDesc{}.SetName("Debug").AddBufferArg(UavBuf("scratch")).SetSideEffect());

	fg.Compile(&NullRm());

	CHECK(fg.ExecutionOrder() == std::vector<std::string>{ "Debug" });
	CHECK_FALSE(fg.WasCulled("Debug"));
}

TEST_CASE("FrameGraph: derives producer -> consumer barriers", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("buf", MakeBuffer(7));  // imported in a 'none' state

	fg.AddPass(PassDesc{}.SetName("Fill").AddBufferArg(UavBuf("buf")));
	fg.AddPass(PassDesc{}.SetName("Read").AddBufferArg(SrvBuf("buf")).SetSideEffect());

	fg.Compile(&NullRm());

	const PassBarriers& fillBarriers = fg.BarriersFor("Fill");
	REQUIRE(fillBarriers.bufferDescs.size() == 1);
	CHECK(fillBarriers.bufferHandles[0].slot.index == 7);
	CHECK(fillBarriers.bufferDescs[0].accessBefore == BarrierAccessFlag::kNone);
	CHECK(fillBarriers.bufferDescs[0].accessAfter == BarrierAccessFlag::kUnorderedAccess);

	const PassBarriers& readBarriers = fg.BarriersFor("Read");
	REQUIRE(readBarriers.bufferDescs.size() == 1);
	CHECK(readBarriers.bufferDescs[0].accessBefore == BarrierAccessFlag::kUnorderedAccess);
	CHECK(readBarriers.bufferDescs[0].accessAfter == BarrierAccessFlag::kShaderResource);
}

TEST_CASE("FrameGraph: emits no barrier when the state is unchanged", "[fg]")
{
	FrameGraph fg;
	// Imported already in the shader-resource state both readers want.
	fg.ImportBuffer(
		"buf",
		MakeBuffer(1),
		AccessState{ BarrierSyncFlag::kPixelShader, BarrierAccessFlag::kShaderResource });

	fg.AddPass(PassDesc{}.SetName("ReadA").AddBufferArg(SrvBuf("buf")).SetSideEffect());
	fg.AddPass(PassDesc{}.SetName("ReadB").AddBufferArg(SrvBuf("buf")).SetSideEffect());

	fg.Compile(&NullRm());

	CHECK(fg.BarriersFor("ReadA").Empty());
	CHECK(fg.BarriersFor("ReadB").Empty());
}

TEST_CASE("FrameGraph: a same-state UAV access still gets a barrier", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("buf", MakeBuffer(3));  // imported in a 'none' state

	fg.AddPass(PassDesc{}.SetName("Write").AddBufferArg(UavBuf("buf")).SetSideEffect());
	fg.AddPass(PassDesc{}.SetName("ReadWrite").AddBufferArg(UavBuf("buf")).SetSideEffect());

	fg.Compile(&NullRm());

	// Nothing about the state changes between the two, but the second reads what the first wrote:
	// without this barrier the dispatches may overlap.
	const PassBarriers& barriers = fg.BarriersFor("ReadWrite");
	REQUIRE(barriers.bufferDescs.size() == 1);
	CHECK(barriers.bufferHandles[0].slot.index == 3);
	CHECK(barriers.bufferDescs[0].accessBefore == BarrierAccessFlag::kUnorderedAccess);
	CHECK(barriers.bufferDescs[0].accessAfter == BarrierAccessFlag::kUnorderedAccess);
}

//
// Error handling
//

TEST_CASE("FrameGraph: GetBuffer on an undeclared buffer throws", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("buf", MakeBuffer(3));
	fg.AddPass(
		PassDesc{}.SetName("P").AddBufferArg(UavBuf("buf")).SetExec([](const PassContext& ctx) {
			(void)ctx.GetBuffer("missing");
		}));

	fg.Compile(&NullRm());

	CommandListHandle  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueHandle queue = core::SharedRef<NullCommandQueue>::Make();
	fg.RegisterQueue("main", queue, cmd);
	CHECK_THROWS_AS(fg.Execute(), std::runtime_error);
}

TEST_CASE("FrameGraph: GetBuffer on a transient (unimported) buffer throws", "[fg]")
{
	FrameGraph fg;
	fg.AddPass(
		PassDesc{}
			.SetName("P")
			.AddBufferArg(UavBuf("scratch"))
			.SetSideEffect()
			.SetExec([](const PassContext& ctx) { (void)ctx.GetBuffer("scratch"); }));

	fg.Compile(&NullRm());

	CommandListHandle  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueHandle queue = core::SharedRef<NullCommandQueue>::Make();
	fg.RegisterQueue("main", queue, cmd);
	CHECK_THROWS_AS(fg.Execute(), std::runtime_error);
}

TEST_CASE("FrameGraph: GetBuffer resolves an imported buffer; imports clear after Execute", "[fg]")
{
	FrameGraph   fg;
	BufferHandle got{};

	fg.ImportBuffer("buf", MakeBuffer(9));
	fg.AddPass(
		PassDesc{}.SetName("P").AddBufferArg(UavBuf("buf")).SetExec([&](const PassContext& ctx) {
			got = ctx.GetBuffer("buf");
		}));

	fg.Compile(&NullRm());
	REQUIRE(fg.ImportedResourceCount() == 1);

	CommandListHandle  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueHandle queue = core::SharedRef<NullCommandQueue>::Make();
	fg.RegisterQueue("main", queue, cmd);
	fg.Execute();

	CHECK(got.slot.index == 9);
	CHECK(fg.ImportedResourceCount() == 0);             // imports dropped after Execute
	CHECK_THROWS_AS(fg.Execute(), std::runtime_error);  // must recompile before next Execute
}

TEST_CASE("FrameGraph: Execute before Compile throws", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("buf", MakeBuffer(1));
	fg.AddPass(PassDesc{}.SetName("P").AddBufferArg(UavBuf("buf")));

	CHECK_THROWS_AS(fg.Execute(), std::runtime_error);
}

TEST_CASE("FrameGraph: a duplicate pass name throws", "[fg]")
{
	FrameGraph fg;
	fg.AddPass(PassDesc{}.SetName("Dup").AddBufferArg(UavBuf("a")).SetSideEffect());

	CHECK_THROWS_AS(
		fg.AddPass(PassDesc{}.SetName("Dup").AddBufferArg(UavBuf("b")).SetSideEffect()),
		std::runtime_error);
}

TEST_CASE("FrameGraph: accessing an imported buffer as a texture throws at Compile", "[fg]")
{
	FrameGraph fg;
	fg.ImportBuffer("res", MakeBuffer(1));
	fg.AddPass(PassDesc{}.SetName("P").AddTextureArg(SrvTex("res")).SetSideEffect());

	CHECK_THROWS_AS(fg.Compile(&NullRm()), std::runtime_error);
}

TEST_CASE("FrameGraph: a texture that is both an attachment and an import throws", "[fg]")
{
	FrameGraph fg;

	TextureHandle tex{};
	tex.slot.index = 5;
	fg.ImportTexture("rt", tex);  // tracked by name...

	RtvHandle rtv{};
	rtv.idx = 99;  // ...and the RTV resolves to the same texture (idx 5).

	MockResourceManager rm;
	rm.rtvToTexture[99] = tex;

	fg.AddPass(PassDesc{}.SetName("Render").AddColorAttachment(rtv));

	CHECK_THROWS_AS(fg.Compile(&rm), std::runtime_error);
}

TEST_CASE("FrameGraph: an attachment-only texture transitions to render target", "[fg]")
{
	FrameGraph fg;

	TextureHandle tex{};
	tex.slot.index = 7;  // never imported -> reached only as an attachment

	RtvHandle rtv{};
	rtv.idx = 1;

	MockResourceManager rm;
	rm.rtvToTexture[1] = tex;

	fg.AddPass(PassDesc{}.SetName("Render").AddColorAttachment(rtv));

	fg.Compile(&rm);

	// One transition, on the attachment's resolved texture, taking it to RT.
	const PassBarriers& barriers = fg.BarriersFor("Render");
	REQUIRE(barriers.textureDescs.size() == 1);
	CHECK(barriers.textureHandles[0].slot.index == 7);
	CHECK(barriers.textureDescs[0].accessAfter == BarrierAccessFlag::kRenderTarget);
	CHECK(barriers.textureDescs[0].layoutAfter == BarrierLayout::kRenderTarget);
}
