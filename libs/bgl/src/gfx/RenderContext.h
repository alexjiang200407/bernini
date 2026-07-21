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
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IRenderTarget.h>
#include <bgl/RenderJob.h>

namespace bgl
{
	/**
	 * One single-threaded submission context over a device: the command list, frame graph, retained
	 * pass objects and GPU-debug readback ring that turn a RenderJob into a submitted, presented
	 * frame. A client drives exactly one context from one thread.
	 *
	 * Backend-agnostic by construction -- it reaches the GPU only through the RHI interfaces, so it
	 * lives in core rather than a backend TU. The device, queue, list and allocator are borrowed from
	 * the owning Graphics for now; a later stage gives each context its own.
	 *
	 * Affinity, not thread-safety: like the rest of bgl, exactly one thread may touch a given context.
	 */
	class RenderContext
	{
	public:
		RenderContext(DeviceRef device, CommandQueueRef queue, ResourceManagerRef resourceManager);

		~RenderContext() noexcept;

		RenderContext(const RenderContext&) noexcept = delete;
		RenderContext(RenderContext&&) noexcept      = delete;

		RenderContext&
		operator=(const RenderContext&) noexcept = delete;

		RenderContext&
		operator=(RenderContext&&) noexcept = delete;

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

		void
		SetGpuAssertionHandler(IGpuAssertionHandler* handler) noexcept
		{
			m_GpuAssertionHandler = handler;
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

		// The device, queue and resource manager are borrowed from the owning Graphics and shared
		// across all contexts today. The command list and its bootstrap allocator are the context's
		// own -- one recorder per context.
		DeviceRef           m_Device;
		CommandQueueRef     m_CommandQueue;
		ResourceManagerRef  m_ResourceManager;
		CommandAllocatorRef m_BootstrapAllocator;
		CommandListRef      m_CommandList;

		bool m_FrameActive = false;

		// The render target bound by the current BeginFrame (null outside a frame).
		RenderTargetBase* m_ActiveTarget = nullptr;

		FrameGraph m_FrameGraph;
		uint32_t   m_DrawCount = 0;

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
