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

		// Non-null only in the poison tests, which pin what the graph records and in what order.
		std::vector<std::string>* log = nullptr;

		void
		WriteBuffer(BufferHandle, const void*, size_t, size_t) noexcept override
		{}
		void
		WriteTexture(TextureHandle, std::span<const TextureSubresourceData>) noexcept override
		{}
		void
		CopyBuffer(BufferHandle dst, BufferHandle, uint64_t, uint64_t, uint64_t) noexcept override
		{
			if (log != nullptr)
			{
				log->push_back(std::format("copy:{}", dst.slot.index));
			}
		}
		void
		CopyBufferToReadback(ReadbackBufferHandle, BufferHandle) noexcept override
		{}
		void
		CopyTextureToReadback(ReadbackBufferHandle, TextureHandle) noexcept override
		{}
		void
		Barrier(BufferHandle handle, const BufferBarrierDesc&) noexcept override
		{
			if (log != nullptr)
			{
				log->push_back(std::format("barrier:{}", handle.slot.index));
			}
		}
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
		{
			if (log != nullptr)
			{
				log->push_back("barriers");
			}
		}
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

		void
		Flush() noexcept override
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

		// Enough of a buffer for a live BufferPoisoner: it creates one pattern buffer, asks for
		// the target's size, and copies. c_PatternHandleIndex is what the poison tests see the
		// one-time pattern upload barrier against.
		static constexpr uint32_t c_PatternHandleIndex = 100;

		BufferHandle
		CreateStructBuffer(const StructBufferDesc&) noexcept override
		{
			return BufferHandle({ c_PatternHandleIndex, 0 }, c_PatternHandleIndex);
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
		SamplerHandle
		CreateSampler(const SamplerDesc&) noexcept override
		{
			return {};
		}
		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc&) noexcept override
		{
			return {};
		}
		void
		RegisterQueue(ICommandQueue*) noexcept override
		{}
		void
		UnregisterQueue(ICommandQueue*) noexcept override
		{}
		void
		DestroyBuffer(BufferHandle, bool) noexcept override
		{}
		void
		DestroyTexture(TextureHandle, bool) noexcept override
		{}
		void
		DestroySampler(SamplerHandle, bool) noexcept override
		{}
		void
		DestroyReadbackBuffer(ReadbackBufferHandle, bool) noexcept override
		{}
		void
		DestroySrv(SrvHandle, bool) noexcept override
		{}
		void
		DestroyRtv(RtvHandle, bool) noexcept override
		{}
		void
		DestroyDsv(DsvHandle, bool) noexcept override
		{}
		void
		CleanupExpiredResources() noexcept override
		{}
		SrvHandle
		CreateSrv(TextureHandle, const SrvDesc&) noexcept override
		{
			return {};
		}
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

		BufferDesc
		GetBufferDesc(BufferHandle) const noexcept override
		{
			// One pattern chunk over, so a poisoned buffer takes two copies.
			return BufferDesc{ 96 * 1024, true, "Mock Buffer" };
		}

		TextureDesc
		GetTextureDesc(TextureHandle) const noexcept override
		{
			return {};
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
			return true;
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
		ValidSrvHandle(const SrvHandle&) const noexcept override
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
		static MockResourceManager g_Rm;
		return g_Rm;
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

	CommandListRef  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueRef queue = core::SharedRef<NullCommandQueue>::Make();
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

	CommandListRef  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueRef queue = core::SharedRef<NullCommandQueue>::Make();
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

	CommandListRef  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueRef queue = core::SharedRef<NullCommandQueue>::Make();
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

namespace
{
	// Everything the poison tests share: a live poisoner over the mock manager, and a command list
	// that records what the graph put on it.
	struct PoisonHarness
	{
		std::vector<std::string>             log;
		core::SharedRef<MockResourceManager> rm    = core::SharedRef<MockResourceManager>::Make();
		core::SharedRef<NullCommandList>     list  = core::SharedRef<NullCommandList>::Make();
		CommandQueueRef                      queue = core::SharedRef<NullCommandQueue>::Make();
		BufferPoisoner                       poisoner;

		PoisonHarness()
		{
			list->log = &log;
			poisoner.Init(rm);
		}

		~PoisonHarness() { poisoner.Release(false); }

		PoisonHarness(const PoisonHarness&) = delete;
		PoisonHarness(PoisonHarness&&)      = delete;

		PoisonHarness&
		operator=(const PoisonHarness&) = delete;

		PoisonHarness&
		operator=(PoisonHarness&&) = delete;

		void
		Run(FrameGraph& fg)
		{
			fg.Compile(rm.Get());
			CommandListRef cmd = list;
			fg.RegisterQueue("main", queue, cmd);
			fg.Execute();
		}
	};
}

// The whole point of poisoning is that the fill lands *between* the transitions the graph derived
// and the dispatches the pass records -- late enough that the buffer is the pass's to write, early
// enough that the pass overwrites the poison. The bracketing single-buffer barriers are what keep
// the fill from racing the pass's own writes.
//
// The mock buffer is half a chunk larger than the poisoner tiles from, so the two copies also pin
// that a buffer bigger than the pattern is covered to its end.
TEST_CASE("FrameGraph: a poisoned arg is filled between the pass barriers and its exec", "[fg]")
{
	PoisonHarness h;

	FrameGraph fg;
	fg.SetBufferPoisoner(&h.poisoner);
	fg.ImportBuffer("scratch", MakeBuffer(7));
	fg.AddPass(
		PassDesc{}
			.SetName("P")
			.AddPoisonedBufferArg("scratch", BarrierSyncFlag::kComputeShader)
			.SetExec([&](const PassContext&) { h.log.push_back("exec"); }));

	h.Run(fg);

	CHECK(
		h.log == std::vector<std::string>{
					 "barriers",   // the pass's own derived barriers
					 "barrier:7",  // scratch -> copy dest
					 std::format(
						 "barrier:{}",
						 MockResourceManager::c_PatternHandleIndex),  // the one-time pattern upload
					 "copy:7",
					 "copy:7",
					 "barrier:7",  // scratch -> back to what the pass declared
					 "exec" });
}

TEST_CASE("FrameGraph: a poison declaration is inert with no poisoner installed", "[fg]")
{
	PoisonHarness h;

	FrameGraph fg;
	fg.ImportBuffer("scratch", MakeBuffer(7));
	fg.AddPass(
		PassDesc{}
			.SetName("P")
			.AddPoisonedBufferArg("scratch", BarrierSyncFlag::kComputeShader)
			.SetExec([&](const PassContext&) { h.log.push_back("exec"); }));

	h.Run(fg);

	CHECK(h.log == std::vector<std::string>{ "barriers", "exec" });
}

// A transient has no handle to fill, and reaches the poison path as a null one rather than as a
// missing name -- deriving the barriers inserts an entry for every name the graph tracks.
TEST_CASE("FrameGraph: a poisoned transient is skipped", "[fg]")
{
	PoisonHarness h;

	FrameGraph fg;
	fg.SetBufferPoisoner(&h.poisoner);
	fg.AddPass(
		PassDesc{}
			.SetName("P")
			.AddPoisonedBufferArg("never.imported", BarrierSyncFlag::kComputeShader)
			.SetSideEffect()
			.SetExec([&](const PassContext&) { h.log.push_back("exec"); }));

	h.Run(fg);

	CHECK(std::ranges::none_of(h.log, [](const std::string& e) { return e.starts_with("copy:"); }));
	CHECK(h.log.back() == "exec");
}

// A view's frustum scopes nest inside the view's: "v0:c1:" is one of view v0's culled frustums, and
// several of them carry buffers under identical names. Resolution therefore has to prefer the
// innermost scope that imported a name and fall outward from there, or N frustums alias one buffer.
TEST_CASE("FrameGraph: name resolution walks outward through nested scopes", "[fg]")
{
	FrameGraph fg;

	fg.ImportBuffer("buf", MakeBuffer(1));
	fg.ImportBuffer("globalOnly", MakeBuffer(5));

	fg.SetResourceNamespace("v0:");
	fg.ImportBuffer("buf", MakeBuffer(2));
	fg.ImportBuffer("viewOnly", MakeBuffer(3));

	fg.SetResourceNamespace("v0:c1:");
	fg.ImportBuffer("buf", MakeBuffer(4));

	BufferHandle innerBuf{};
	BufferHandle viewOnly{};
	BufferHandle globalOnly{};
	fg.AddPass(
		PassDesc{}
			.SetName("Frustum")
			.AddBufferArg(UavBuf("buf"))
			.AddBufferArg(UavBuf("viewOnly"))
			.AddBufferArg(UavBuf("globalOnly"))
			.SetExec([&](const PassContext& ctx) {
				innerBuf   = ctx.GetBuffer("buf");
				viewOnly   = ctx.GetBuffer("viewOnly");
				globalOnly = ctx.GetBuffer("globalOnly");
			}));

	fg.SetResourceNamespace("v0:");

	BufferHandle viewBuf{};
	fg.AddPass(
		PassDesc{}.SetName("View").AddBufferArg(UavBuf("buf")).SetExec([&](const PassContext& ctx) {
			viewBuf = ctx.GetBuffer("buf");
		}));

	fg.SetResourceNamespace("");

	BufferHandle bareBuf{};
	fg.AddPass(
		PassDesc{}.SetName("Bare").AddBufferArg(UavBuf("buf")).SetExec([&](const PassContext& ctx) {
			bareBuf = ctx.GetBuffer("buf");
		}));

	fg.Compile(&NullRm());

	CommandListRef  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueRef queue = core::SharedRef<NullCommandQueue>::Make();
	fg.RegisterQueue("main", queue, cmd);
	fg.Execute();

	CHECK(innerBuf.slot.index == 4);    // the frustum's own, not the view's and not the global one
	CHECK(viewOnly.slot.index == 3);    // one scope out
	CHECK(globalOnly.slot.index == 5);  // all the way out
	CHECK(viewBuf.slot.index == 2);     // the inner scope's import does not leak outward
	CHECK(bareBuf.slot.index == 1);
}

// The other direction of the same rule. Nothing relies on it today, but it is what stops a buffer
// that was meant to be per-frustum from being reachable -- and silently shared -- from the view.
TEST_CASE("FrameGraph: an outer scope cannot name an inner scope's import", "[fg]")
{
	FrameGraph fg;

	fg.SetResourceNamespace("v0:c1:");
	fg.ImportBuffer("frustumOnly", MakeBuffer(7));

	fg.SetResourceNamespace("v0:");

	// Unresolvable, so it is a transient: it takes part in ordering by name but has no handle.
	fg.AddPass(
		PassDesc{}
			.SetName("View")
			.AddBufferArg(UavBuf("frustumOnly"))
			.SetSideEffect()
			.SetExec([](const PassContext& ctx) { (void)ctx.GetBuffer("frustumOnly"); }));

	fg.Compile(&NullRm());

	CommandListRef  cmd   = core::SharedRef<NullCommandList>::Make();
	CommandQueueRef queue = core::SharedRef<NullCommandQueue>::Make();
	fg.RegisterQueue("main", queue, cmd);

	CHECK_THROWS_AS(fg.Execute(), std::runtime_error);
}
