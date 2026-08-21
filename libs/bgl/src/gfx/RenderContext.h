#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "debug/DebugBuffer.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "gfx/RenderTargetBase.h"
#include "passes/BoneOverlayPass.h"
#include "passes/BrdfLutGenPass.h"
#include "passes/CompactInstancesPass.h"
#include "passes/ForwardPass.h"
#include "passes/OutlineMaskPass.h"
#include "passes/PostProcessPass.h"
#include "passes/PreparePresentPass.h"
#include "passes/SkinnedPosePass.h"
#include "passes/SkyboxPass.h"
#include "passes/TaaResolvePass.h"
#include "passes/TransparentSortPass.h"
#include "resource/ResourceManager.h"
#include <bgl/IGraphics.h>

namespace bgl
{
	/**
	 * The frame path behind IGraphics: the command queue, list, frame graph, retained pass objects
	 * and GPU-debug readback ring that turn a RenderJob into a submitted, presented frame.
	 *
	 * Backend-agnostic by construction -- it reaches the GPU only through the RHI interfaces, so it
	 * lives in core rather than a backend TU, and a second backend reuses it as it stands.
	 */
	class RenderContext final
	{
	public:
		RenderContext(DeviceRef device, ResourceManagerRef resourceManager, bool enableDebug);

		~RenderContext() noexcept;

		RenderContext(const RenderContext&) noexcept = delete;
		RenderContext(RenderContext&&) noexcept      = delete;

		RenderContext&
		operator=(const RenderContext&) noexcept = delete;

		RenderContext&
		operator=(RenderContext&&) noexcept = delete;

		RenderTargetRef
		CreateRenderTarget(const RenderTargetDesc& desc);

		void
		BeginFrame(const RenderTargetRef& target);

		void
		Draw(const RenderJob& job);

		void
		EndFrame();

		void
		Resize(const RenderTargetRef& target, uint32_t width, uint32_t height);

		void
		SetRenderScale(const RenderTargetRef& target, float scale);

		void
		ScreenshotPng(const RenderTargetRef& target, const std::string& filepath);

		assetlib::ImageData
		ScreenshotToMemory(const RenderTargetRef& target);

		CaptureTicket
		SubmitCapture(const RenderTargetRef& target);

		std::optional<assetlib::ImageData>
		TryResolveCapture(CaptureTicket ticket);

		void
		DiscardCapture(CaptureTicket ticket) noexcept;

		void
		SetGpuAssertionHandler(IGpuAssertionHandler* handler) noexcept
		{
			m_GpuAssertionHandler = handler;
		}

		// The submission timeline. A render target presents on the queue its frames are recorded
		// on, so every target must be created against this queue.
		[[nodiscard]] CommandQueueRef
		GetCommandQueueCpy() const noexcept
		{
			return m_CommandQueue;
		}

		void
		WaitIdle() noexcept
		{
			m_CommandQueue->Flush();
		}

		void
		DiscardPendingGpuAssertions() noexcept;

	private:
#if defined(BERNINI_GPU_DEBUG)
		// Maps the GPU-assertion readback for a completed frame slot and crashes via gfatal if any
		// dbg_raise() fired. No-op if the slot has no pending snapshot.
		void
		InspectDebugSlot(uint32_t index);
#endif

		// Shared body of the Screenshot* entry points. `caller` names the one that asked, so a
		// mid-frame call reports the name the user wrote.
		assetlib::ImageData
		CaptureBackbuffer(const RenderTargetRef& target, std::string_view caller);

		/**
		 * One in-flight backbuffer capture: its own allocator (the frame ring's cannot be reset
		 * without waiting on the frame), the readback the copy lands in, and the layout snapshot
		 * the resolve decodes with. `ticketId` 0 means the slot is free; `fence` outlives the
		 * ticket so a slot freed by a discard is not re-recorded while its copy is in flight.
		 */
		struct CaptureSlot
		{
			CommandAllocatorRef   allocator;
			ReadbackBufferHandle  readback;
			uint64_t              fence    = 0;
			uint64_t              ticketId = 0;
			TextureReadbackLayout layout;
			uint32_t              width  = 0;
			uint32_t              height = 0;
			Format                format{};
		};

		// The slot a live ticket names. @throws GraphicsError if the ticket is null or spent.
		[[nodiscard]] CaptureSlot&
		FindCapture(CaptureTicket ticket);

		CaptureTicket
		SubmitCaptureImpl(const RenderTargetRef& target, std::string_view caller);

		DeviceRef           m_Device;
		CommandQueueRef     m_CommandQueue;
		ResourceManagerRef  m_ResourceManager;
		CommandAllocatorRef m_BootstrapAllocator;
		CommandListRef      m_CommandList;

		bool m_EnableDebug = false;
		bool m_FrameActive = false;

		// The render target bound by the current BeginFrame (null outside a frame).
		RenderTargetBase* m_ActiveTarget = nullptr;

		// Whether this frame's Draw saw the same unjittered camera as the frame before. Consumed
		// by the TAA resolve, which must not treat empty pixels -- whose motion vector is zero
		// regardless of the camera -- as being at rest during a pan.
		bool m_CameraStill = false;

		// Whether any of this frame's draws changed in a way no motion vector describes -- a material
		// rebound, rewritten or deleted, an environment replaced, an instance placed or deleted. The
		// accumulation describes a scene that no longer exists, so the resolve is told to start
		// again from this frame.
		bool m_TemporalBreak = false;

		// Whether any of this frame's draws attached an outline-mask pass; what tells the
		// post-process the mask holds this frame's content rather than a cleared texture.
		bool m_OutlineMaskDrawn = false;

		// The overlay's toggle as it stood when this frame imported its resources. Every later
		// decision reads this rather than the target, so a toggle flipped mid-frame cannot attach a
		// pass naming a texture the frame never imported.
		bool m_BoneOverlayActive = false;

		// Whether any draw this frame put bones down. The composite is skipped without it, so a
		// frame whose views place nothing skinned does not sample a target that was only cleared.
		bool m_BoneOverlayDrawn = false;

		FrameGraph m_FrameGraph;
		uint32_t   m_DrawCount = 0;

		// Frames begun by this context, counting up forever. Unrelated to a render target's
		// FrameIndex, which cycles over the backbuffer ring.
		uint64_t m_FrameCounter = 0;

		// The last draw's unjittered camera pair, for the resolve; see Draw.
		glm::mat4 m_TaaClipToView{ 1.0f };
		glm::mat4 m_TaaViewToPrevClip{ 1.0f };
		glm::vec2 m_TaaJitter{ 0.0f };

		std::array<CaptureSlot, IGraphics::c_MaxPendingCaptures> m_Captures;
		uint64_t                                                 m_NextCaptureId = 1;

		BrdfLutGenPass       m_BrdfLut;
		PreparePresentPass   m_PreparePresentPass;
		ForwardPass          m_Forward;
		SkyboxPass           m_Skybox;
		PostProcessPass      m_PostProcess;
		OutlineMaskPass      m_OutlineMask;
		TaaResolvePass       m_TaaResolve;
		CompactInstancesPass m_CompactInstances;
		SkinnedPosePass      m_SkinnedPose;
		BoneOverlayPass      m_BoneOverlay;
		TransparentSortPass  m_TransparentSort;

		SamplerHandle m_PointClampSampler;

		// The reprojected history lands between texels, so it is the one thing here that is filtered.
		SamplerHandle m_LinearClampSampler;

		IGpuAssertionHandler* m_GpuAssertionHandler = nullptr;

#if defined(BERNINI_GPU_DEBUG)
		// GPU-based assertions (dbg_raise). One shared UAV bound frame-wide into every pipeline's
		// implicit gDebug cbuffer; copied to a per-frame-in-flight readback ring at EndFrame and
		// inspected two frames later at BeginFrame. Fully compiled out of Release. NOTE: only the
		// "main" queue is bound today -- async-compute passes would each need their own debug buffer
		// bound on their command list. Capacity is small on purpose: the whole buffer is copied to
		// readback every frame, and a handful of records is enough since we crash on the first frame
		// that fires. 256 records -> ~4 KB (header + 256*16 B).
		static constexpr uint32_t c_DebugBufferCapacity = 256;

		// Installed on the frame graph, which drives it for the buffer args passes declare poisoned.
		BufferPoisoner m_BufferPoisoner;

		DebugBuffer          m_DebugBuffer;
		ReadbackBufferHandle m_DebugReadbacks[c_SwapchainImageCount];
		bool                 m_DebugReadbackPending[c_SwapchainImageCount] = {};

		// The fence that gates each slot's copy. A slot is context-wide but every RenderTarget indexes
		// it with a frame index of its own, so the target that inspects a slot need not be the one that
		// filled it -- and must not wait on its own fence to decide the copy has landed.
		uint64_t m_DebugReadbackFence[c_SwapchainImageCount] = {};
#endif
	};
}
