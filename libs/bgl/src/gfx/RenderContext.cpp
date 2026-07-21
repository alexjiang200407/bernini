#include "gfx/RenderContext.h"

#include "constants/constants.h"
#include "debug/DebugReadback.h"
#include "passes/ClearPass.h"
#include "passes/DrawData.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include <bgl/IGraphics.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace bgl
{
	namespace
	{
		// Backbuffer readbacks come back as B8G8R8A8; these formats need R/B swapped to write RGBA.
		bool
		isBgra(Format format)
		{
			return format == Format::BGRA8_UNORM || format == Format::SBGRA8_UNORM;
		}

		bool
		isSrgb(Format format)
		{
			return format == Format::SRGBA8_UNORM || format == Format::SBGRA8_UNORM;
		}

		// A mapped GPU readback as an RGBA8 image: drops the padding D3D12 aligns each row to, and
		// swaps R and B if the backbuffer was BGRA. `src` already points past the readback's base
		// offset.
		assetlib::ImageData
		readbackToImage(
			const uint8_t* src,
			size_t         rowPitch,
			uint32_t       width,
			uint32_t       height,
			Format         format)
		{
			if (src == nullptr)
			{
				throw GraphicsError("Screenshot: null readback source");
			}
			if (width == 0 || height == 0)
			{
				throw GraphicsError(
					std::format("Screenshot: invalid dimensions {}x{}", width, height));
			}

			const size_t tightPitch = static_cast<size_t>(width) * 4;

			auto image     = assetlib::ImageData();
			image.width    = width;
			image.height   = height;
			image.vkFormat = isSrgb(format) ? assetlib::VkFormat::R8G8B8A8_SRGB :
			                                  assetlib::VkFormat::R8G8B8A8_UNORM;
			image.pixels   = core::fixed_buffer<std::byte>(tightPitch * height);
			image.subresources.push_back({ 0, tightPitch, tightPitch * height });

			const bool bgra = isBgra(format);
			auto*      dst  = reinterpret_cast<uint8_t*>(image.pixels.data());

			for (uint32_t y = 0; y < height; ++y)
			{
				const uint8_t* row = src + static_cast<size_t>(y) * rowPitch;
				uint8_t*       out = dst + static_cast<size_t>(y) * tightPitch;
				for (uint32_t x = 0; x < width; ++x)
				{
					const uint8_t* p = row + static_cast<size_t>(x) * 4;
					uint8_t*       o = out + static_cast<size_t>(x) * 4;
					o[0]             = bgra ? p[2] : p[0];
					o[1]             = p[1];
					o[2]             = bgra ? p[0] : p[2];
					o[3]             = p[3];
				}
			}

			return image;
		}

		// Encodes a tight RGBA8 image as a PNG via stb_image_write -- cross-platform, replacing the
		// old DirectXTex DDS / WIC PNG encoders.
		void
		writePng(const std::string& filepath, const assetlib::ImageData& image)
		{
			if (const std::filesystem::path parent = std::filesystem::path(filepath).parent_path();
			    !parent.empty())
			{
				std::error_code ec;
				std::filesystem::create_directories(parent, ec);
			}

			if (stbi_write_png(
					filepath.c_str(),
					static_cast<int>(image.width),
					static_cast<int>(image.height),
					4,
					image.pixels.data(),
					static_cast<int>(image.width) * 4) == 0)
			{
				throw GraphicsError(
					std::format(
						"Screenshot failed to write PNG '{}' ({}x{}) -- path may be unwritable "
						"or the disk is full",
						filepath,
						image.width,
						image.height));
			}
		}
	}

	// Graph resource name of the active target's backbuffer.
	constexpr std::string_view c_BackbufferName = "backbuffer";

	RenderContext::RenderContext(
		DeviceRef          device,
		CommandQueueRef    queue,
		ResourceManagerRef resourceManager) :
		m_Device(std::move(device)), m_CommandQueue(std::move(queue)),
		m_ResourceManager(std::move(resourceManager))
	{
		// This context's queue is one of the timelines a deferred destroy must clear before the
		// resource manager reclaims a slot.
		m_ResourceManager->RegisterQueue(m_CommandQueue.Get());

		// The list and its allocator are the context's own -- one recorder per context, so two
		// contexts can record concurrently. The queue is still shared with Graphics for now.
		m_BootstrapAllocator = m_Device->CreateCommandAllocator();

		auto cmdListDesc = CommandListDesc();
		cmdListDesc.type = QueueType::kGraphics;
		m_CommandList =
			m_Device->CreateCommandList(cmdListDesc, m_BootstrapAllocator, m_ResourceManager);

		m_CompactInstances.Init(m_Device, m_ResourceManager);
		m_TransparentSort.Init(m_Device, m_ResourceManager);
		m_Forward.Init(m_Device);
		m_Skybox.Init(m_Device);

#if defined(BERNINI_GPU_DEBUG)
		m_DebugBuffer.Init(c_DebugBufferCapacity, m_ResourceManager);
		for (auto& readback : m_DebugReadbacks)
		{
			auto rbDesc      = ReadbackBufferDesc();
			rbDesc.byteSize  = m_DebugBuffer.ByteSize();
			rbDesc.debugName = "GPU Debug Readback";
			readback         = m_ResourceManager->CreateReadbackBuffer(rbDesc);
		}
#endif
	}

	RenderContext::~RenderContext() noexcept
	{
		logger::trace("~RenderContext");

		// Idle the GPU so nothing in flight still references what the passes and the debug ring are
		// about to release, then drop this queue from the resource manager's timeline set -- it has
		// completed, so any remaining deferred free gated on it is now satisfiable.
		m_CommandQueue->Flush();
		m_ResourceManager->UnregisterQueue(m_CommandQueue.Get());

		// The GPU is idle, so these frees are immediate (deferred = false), needing no gate.
		m_Forward.Release();
		m_Skybox.Release();
		m_CompactInstances.Release(false);
		m_TransparentSort.Release(false);

#if defined(BERNINI_GPU_DEBUG)
		// The GPU is idle, so assertions from the final frames whose slot was never reused by a later
		// BeginFrame are now safe to inspect -- drain them so tail-frame assertions are not missed.
		for (uint32_t i = 0; i < c_SwapchainImageCount; ++i)
		{
			InspectDebugSlot(i);
		}
		for (auto& readback : m_DebugReadbacks)
		{
			m_ResourceManager->DestroyReadbackBuffer(readback, false);
		}
		m_DebugBuffer.Release(false);
#endif

		// Clear retained passes; each pass descriptor holds a resource-manager reference that would
		// otherwise keep the manager alive past the owning Graphics's live-object report.
		m_FrameGraph.Reset();
	}

	void
	RenderContext::DiscardPendingGpuAssertions() noexcept
	{
#if defined(BERNINI_GPU_DEBUG)
		// Abandon every un-inspected readback slot so InspectDebugSlot early-returns for it. The
		// snapshots were already copied out; we simply choose not to read them, dropping the
		// assertions instead of reporting or crashing on them.
		for (bool& pending : m_DebugReadbackPending)
		{
			pending = false;
		}
#endif
	}

#if defined(BERNINI_GPU_DEBUG)
	void
	RenderContext::InspectDebugSlot(uint32_t index)
	{
		if (!m_DebugReadbackPending[index])
		{
			return;
		}
		m_DebugReadbackPending[index] = false;

		// Not the caller's rt.FrameFence(index): that gates the caller's own last frame at this
		// slot, which says nothing about a copy another target submitted into the same slot. A target
		// that has never drawn here has no fence at all, so BeginFrame waits on nothing and would map
		// a buffer the GPU is still writing.
		m_CommandQueue->WaitForFenceCPUBlocking(m_DebugReadbackFence[index]);

		const void* mapped = m_ResourceManager->MapReadback(m_DebugReadbacks[index]);
		gassert(mapped != nullptr, "Failed to map GPU debug readback");

		const auto report = InspectDebugReadback(mapped, c_DebugBufferCapacity);
		m_ResourceManager->UnmapReadback(m_DebugReadbacks[index]);

		if (!report.has_value())
		{
			return;
		}

		std::string msg = std::format(
			"GPU assertion(s) fired: {} raised{}",
			report->count,
			report->overflow ? " (debug buffer overflowed; some records dropped)" : "");

		// Identical records are the norm rather than the exception: one bad submesh raises once per
		// vertex, so the interesting thing is which distinct failures happened, not a thousand copies
		// of one. Ordered by first appearance, because that is the one that has a cause.
		auto seen = std::vector<std::pair<idl::DebugRecord, uint32_t>>();
		for (const idl::DebugRecord& rec : report->records)
		{
			const auto same = [&rec](const std::pair<idl::DebugRecord, uint32_t>& entry) {
				return entry.first.errcode == rec.errcode && entry.first.value == rec.value &&
				       entry.first.limit == rec.limit && entry.first.context == rec.context;
			};

			if (const auto it = std::ranges::find_if(seen, same); it != seen.end())
				++it->second;
			else
				seen.emplace_back(rec, 1u);
		}

		for (const auto& [rec, times] : seen)
		{
			msg += std::format(
				"\n  {} value={} limit={} context={} (x{})",
				ErrorCodeName(rec.errcode),
				rec.value,
				rec.limit,
				rec.context,
				times);
		}

		if (m_GpuAssertionHandler != nullptr)
		{
			logger::error("{}", msg);

			std::vector<uint32_t> errcodes;
			errcodes.reserve(report->records.size());
			for (const idl::DebugRecord& rec : report->records)
			{
				errcodes.push_back(rec.errcode);
			}

			GpuAssertionReport pub;
			pub.raisedCount = report->count;
			pub.overflow    = report->overflow;
			pub.errcodes    = std::span<const uint32_t>(errcodes.data(), errcodes.size());

			m_GpuAssertionHandler->OnGpuAssertion(pub);
			return;
		}

		gfatal("{}", msg);
	}
#endif

	void
	RenderContext::BeginFrame(const RenderTargetRef& target)
	{
		if (m_FrameActive)
		{
			throw GraphicsError("BeginFrame called while a frame is already active");
		}

		m_ActiveTarget = target->As<RenderTargetBase>();
		gassert(m_ActiveTarget != nullptr, "BeginFrame requires a valid RenderTarget");

		RenderTargetBase& rt    = *m_ActiveTarget;
		const uint32_t    index = rt.FrameIndex();

		uint64_t fenceToWaitOn = rt.FrameFence(index);
		if (fenceToWaitOn != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(fenceToWaitOn);
		}

#if defined(BERNINI_GPU_DEBUG)
		// This slot's fence has completed, so the GPU-assertion snapshot it copied out
		// (two frames ago) is now safe to read. Crashes if any assertion fired.
		InspectDebugSlot(index);
#endif

		rt.FrameAllocator(index)->ResetAllocator();

		m_CommandList->Open(m_CommandQueue.Get(), rt.FrameAllocator(index));

#if defined(BERNINI_GPU_DEBUG)
		// Zero the debug buffer's header for this frame, hand it to the shaders as a UAV,
		// and bind it frame-wide so every dbg_raise() lands in it. The buffer is left in
		// copy-dest by the previous EndFrame (and by creation on the first frame), so the
		// reset WriteBuffer needs no pre-barrier.
		m_CommandList->BeginEvent("GPU Debug Buffer Reset");
		m_DebugBuffer.Reset(m_CommandList.Get());
		m_CommandList->Barrier(
			m_DebugBuffer.GetBufferHandle(),
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kCopy)
				.AddAccessBefore(BarrierAccessFlag::kCopyDest)
				.AddSyncAfter(BarrierSyncFlag::kAllCommands)
				.AddAccessAfter(BarrierAccessFlag::kUnorderedAccess));
		m_CommandList->EndEvent();
		m_CommandList->SetActiveDebugBuffer(m_DebugBuffer.GetBufferHandle());
#endif

		m_FrameGraph.Reset();
		m_DrawCount = 0;
		m_FrameGraph.RegisterQueue("main", m_CommandQueue, m_CommandList);
		m_FrameGraph.ImportTexture(
			std::string(c_BackbufferName),
			rt.BackbufferTexture(index),
			AccessState{ BarrierSyncFlag::kNone,
		                 BarrierAccessFlag::kNone,
		                 BarrierLayout::kPresent });

		const std::array<ClearPass::ColorTarget, 1> colorTargets{
			{ { std::string(c_BackbufferName),
			    rt.BackbufferRtv(index),
			    { 0.0f, 0.0f, 0.0f, 1.0f } } }
		};
		ClearPass()
			.AttachToFrameGraph(m_FrameGraph, m_ResourceManager.Get(), colorTargets, rt.DepthDsv());

		m_FrameActive = true;
	}

	void
	RenderContext::Draw(const RenderJob& job)
	{
		if (!m_FrameActive)
		{
			throw GraphicsError("Draw must be called between BeginFrame and EndFrame");
		}

		if (job.view == nullptr)
		{
			throw GraphicsError("RenderJob passed to Draw requires a SceneView");
		}

		auto       view_    = job.view->As<SceneView>();
		auto       scene_   = view_->GetScene()->As<Scene>();
		const auto viewport = job.viewport;
		const auto viewProj = job.camera.GetViewProjection();

		const uint32_t drawIdx = m_DrawCount++;

		m_FrameGraph.SetResourceNamespace(view_->ResourceNamespace());

		scene_->AttachToFrameGraph(m_FrameGraph, drawIdx);
		view_->AttachToFrameGraph(m_FrameGraph, drawIdx);

		auto draw              = DrawData();
		draw.drawIdx           = drawIdx;
		draw.view              = job.view;
		draw.viewport          = viewport;
		draw.viewProj          = viewProj;
		draw.backBufferHandle  = m_ActiveTarget->BackbufferRtv(m_ActiveTarget->FrameIndex());
		draw.depthBufferHandle = m_ActiveTarget->DepthDsv();
		draw.backBufferName    = std::string(c_BackbufferName);

		draw.anisoLinearWrapSampler = scene_->GetSampler(Scene::StandardSampler::kAnisoLinearWrap);
		draw.linearClampSampler     = scene_->GetSampler(Scene::StandardSampler::kLinearClamp);

		draw.cameraPos = glm::vec3(glm::inverse(job.camera.GetView())[3]);

		draw.env      = view_->GetEnvironmentMap();
		draw.exposure = view_->GetExposure();
		draw.skybox   = view_->GetSkybox();

		glm::mat4 viewNoTranslation = job.camera.GetView();
		viewNoTranslation[3]        = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 clipToWorld       = glm::inverse(job.camera.GetProjection() * viewNoTranslation);

		if (draw.skybox.has_value())
		{
			if (draw.skybox->rotationY != 0.0f)
			{
				clipToWorld = glm::rotate(
								  glm::mat4(1.0f),
								  draw.skybox->rotationY,
								  glm::vec3(0.0f, 1.0f, 0.0f)) *
				              clipToWorld;
			}
			draw.skyboxClipToWorld = clipToWorld;
			m_Skybox.AttachToFrameGraph(m_FrameGraph, draw);
		}

		m_TransparentSort.AttachToFrameGraph(m_FrameGraph, draw);
		m_CompactInstances.AttachToFrameGraph(m_FrameGraph, draw);
		m_Forward.AttachToFrameGraph(m_FrameGraph, draw);
	}

	void
	RenderContext::EndFrame()
	{
		if (!m_FrameActive)
		{
			throw GraphicsError("EndFrame called without a matching BeginFrame");
		}

		RenderTargetBase& rt    = *m_ActiveTarget;
		const uint32_t    index = rt.FrameIndex();

		m_FrameGraph.SetResourceNamespace("");
		m_PreparePresentPass.AttachToFrameGraph(m_FrameGraph, std::string(c_BackbufferName));

		m_FrameGraph.Compile(m_ResourceManager.Get());
		m_FrameGraph.Execute();

#if defined(BERNINI_GPU_DEBUG)
		// Snapshot this frame's GPU assertions into the slot's readback buffer, then
		// leave the debug buffer in copy-dest ready for next frame's reset. The copy
		// rides this command list, gated by the fence recorded below; it is inspected at
		// the next BeginFrame that lands on this slot, whichever target that belongs to.
		m_CommandList->BeginEvent("GPU Debug Buffer Readback");
		m_CommandList->Barrier(
			m_DebugBuffer.GetBufferHandle(),
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kAllCommands)
				.AddAccessBefore(BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(BarrierSyncFlag::kCopy)
				.AddAccessAfter(BarrierAccessFlag::kCopySource));
		m_CommandList->CopyBufferToReadback(
			m_DebugReadbacks[index],
			m_DebugBuffer.GetBufferHandle());
		m_CommandList->Barrier(
			m_DebugBuffer.GetBufferHandle(),
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kCopy)
				.AddAccessBefore(BarrierAccessFlag::kCopySource)
				.AddSyncAfter(BarrierSyncFlag::kCopy)
				.AddAccessAfter(BarrierAccessFlag::kCopyDest));
		m_CommandList->EndEvent();
		m_DebugReadbackPending[index] = true;
#endif

		m_CommandList->Close();

		const uint64_t frameFence = m_CommandQueue->ExecuteCommandList(m_CommandList);
		rt.SetFrameFence(index, frameFence);

#if defined(BERNINI_GPU_DEBUG)
		// The readback copy rode the list just submitted, so this is what gates it.
		m_DebugReadbackFence[index] = frameFence;
#endif

		rt.PresentAndAdvance();

		m_ResourceManager->CleanupExpiredResources();

		m_ActiveTarget = nullptr;
		m_FrameActive  = false;
	}

	void
	RenderContext::Resize(const RenderTargetRef& target, uint32_t width, uint32_t height)
	{
		if (m_FrameActive)
		{
			throw GraphicsError("Resize cannot be called between BeginFrame and EndFrame");
		}

		if (width == 0 || height == 0)
		{
			throw GraphicsError("Resize dimensions must be non-zero");
		}

		RenderTargetBase& rt = *target->As<RenderTargetBase>();

		if (rt.GetWidth() == width && rt.GetHeight() == height)
		{
			return;
		}

		// Idle the GPU so no in-flight frame still references the render targets we
		// are about to release.
		m_CommandQueue->Flush();

		// Reset the command list so it drops its references to the old backbuffers;
		// the swap chain cannot be resized while any reference to them is alive.
		m_BootstrapAllocator->ResetAllocator();
		m_CommandList->Open(m_CommandQueue.Get(), m_BootstrapAllocator.Get());
		m_CommandList->Close();

		rt.ResizeBackbuffers(width, height);
	}

	// The last presented backbuffer of `target`, read back into a tight RGBA8 image. Blocks on the
	// shared queue twice: once for the frame that produced the backbuffer, once for the copy below.
	assetlib::ImageData
	RenderContext::CaptureBackbuffer(const RenderTargetRef& target, std::string_view caller)
	{
		if (m_FrameActive)
		{
			throw GraphicsError(
				std::format("{} cannot be called between BeginFrame and EndFrame", caller));
		}

		RenderTargetBase& rt = *target->As<RenderTargetBase>();

		const uint32_t index         = rt.LastPresentedIndex();
		TextureHandle  textureHandle = rt.BackbufferTexture(index);

		// Make sure the frame that produced this backbuffer has finished.
		if (rt.FrameFence(index) != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(rt.FrameFence(index));
		}

		auto layout = m_ResourceManager->GetTextureReadbackLayout(textureHandle);

		auto readbackDesc      = ReadbackBufferDesc();
		readbackDesc.byteSize  = layout.totalBytes;
		readbackDesc.debugName = "Screenshot Readback";

		auto readback = m_ResourceManager->CreateReadbackBuffer(readbackDesc);

		rt.FrameAllocator(index)->ResetAllocator();
		m_CommandList->Open(m_CommandQueue.Get(), rt.FrameAllocator(index));

		{
			auto barrier = TextureBarrierDesc();
			barrier.AddSyncBefore(BarrierSyncFlag::kNone)
				.AddAccessBefore(BarrierAccessFlag::kNone)
				.SetLayoutBefore(BarrierLayout::kPresent)
				.AddSyncAfter(BarrierSyncFlag::kCopy)
				.AddAccessAfter(BarrierAccessFlag::kCopySource)
				.SetLayoutAfter(BarrierLayout::kCopySource);
			m_CommandList->Barrier(textureHandle, barrier);
		}

		m_CommandList->CopyTextureToReadback(readback, textureHandle);

		{
			auto barrier = TextureBarrierDesc();
			barrier.AddSyncBefore(BarrierSyncFlag::kCopy)
				.AddAccessBefore(BarrierAccessFlag::kCopySource)
				.SetLayoutBefore(BarrierLayout::kCopySource)
				.AddSyncAfter(BarrierSyncFlag::kNone)
				.AddAccessAfter(BarrierAccessFlag::kNone)
				.SetLayoutAfter(BarrierLayout::kPresent);
			m_CommandList->Barrier(textureHandle, barrier);
		}

		m_CommandList->Close();

		uint64_t fence = m_CommandQueue->ExecuteCommandList(m_CommandList);
		m_CommandQueue->WaitForFenceCPUBlocking(fence);

		const TextureDesc texDesc = m_ResourceManager->GetTextureDesc(textureHandle);

		const void* mapped = m_ResourceManager->MapReadback(readback);

		try
		{
			auto image = readbackToImage(
				static_cast<const uint8_t*>(mapped) + layout.offset,
				static_cast<size_t>(layout.rowPitch),
				texDesc.width,
				texDesc.height,
				texDesc.format);

			m_ResourceManager->UnmapReadback(readback);
			m_ResourceManager->DestroyReadbackBuffer(readback, false);
			return image;
		}
		catch (...)
		{
			m_ResourceManager->UnmapReadback(readback);
			m_ResourceManager->DestroyReadbackBuffer(readback, false);
			throw;
		}
	}

	void
	RenderContext::ScreenshotPng(const RenderTargetRef& target, const std::string& filepath)
	{
		writePng(filepath, CaptureBackbuffer(target, "ScreenshotPng"));
	}

	assetlib::ImageData
	RenderContext::ScreenshotToMemory(const RenderTargetRef& target)
	{
		return CaptureBackbuffer(target, "ScreenshotToMemory");
	}
}
