#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "debug/DebugBuffer.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "gfx/RenderTargetBase.h"
#include "passes/CompactInstancesPass.h"
#include "passes/ForwardPass.h"
#include "passes/PreparePresentPass.h"
#include "passes/SkyboxPass.h"
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

		FrameGraph m_FrameGraph;
		uint32_t   m_DrawCount = 0;

		// Monotonic across every target this context drives; a SceneView uses it only to tell one
		// frame from the next when rolling its previous-frame camera over.
		uint64_t m_FrameId = 0;

		std::array<CaptureSlot, IGraphics::c_MaxPendingCaptures> m_Captures;
		uint64_t                                                 m_NextCaptureId = 1;

		PreparePresentPass   m_PreparePresentPass;
		ForwardPass          m_Forward;
		SkyboxPass           m_Skybox;
		CompactInstancesPass m_CompactInstances;
		TransparentSortPass  m_TransparentSort;

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
