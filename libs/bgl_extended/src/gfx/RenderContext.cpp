#include "gfx/RenderContext.h"

#include "constants/constants.h"
#include "culling/Frustum.h"
#include "debug/DebugReadback.h"
#include "passes/ClearPass.h"
#include "passes/DrawData.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/jitter.h"
#include "util/util.h"
#include <bgl/IGraphics.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace bgl
{
	namespace
	{
		// The one frustum a Draw culls against. Shadow cascades will take 1..N.
		constexpr uint32_t c_CameraCullIdx = 0;

		// An output-space viewport on the grid the geometry passes render into. The identity at
		// scale 1.0, where the two grids are the same size.
		Viewport
		ToRenderViewport(const RenderTargetBase& rt, const Viewport& viewport)
		{
			const float x =
				static_cast<float>(rt.GetRenderWidth()) / static_cast<float>(rt.GetWidth());
			const float y =
				static_cast<float>(rt.GetRenderHeight()) / static_cast<float>(rt.GetHeight());

			return Viewport(
				viewport.minX * x,
				viewport.maxX * x,
				viewport.minY * y,
				viewport.maxY * y,
				viewport.minZ,
				viewport.maxZ);
		}

		// Backbuffer readbacks come back as B8G8R8A8; these formats need R/B swapped to write RGBA.
		bool
		IsBgra(Format format)
		{
			return format == Format::BGRA8_UNORM || format == Format::SBGRA8_UNORM;
		}

		// A mapped GPU readback as an RGBA8 image: drops the padding D3D12 aligns each row to, and
		// swaps R and B if the backbuffer was BGRA. `src` already points past the readback's base
		// offset.
		assetlib::ImageData
		ReadbackToImage(
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
			image.vkFormat = GetFormatInfo(format).isSRGB ? assetlib::VkFormat::R8G8B8A8_SRGB :
			                                                assetlib::VkFormat::R8G8B8A8_UNORM;
			image.pixels   = core::fixed_buffer<std::byte>(tightPitch * height);
			image.subresources.push_back({ 0, tightPitch, tightPitch * height });

			const bool bgra = IsBgra(format);
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

		std::string
		GetHistoryName(uint32_t index)
		{
			return std::format("{}{}", c_HistoryName, index);
		}

		// Encodes a tight RGBA8 image as a PNG via stb_image_write -- cross-platform, replacing the
		// old DirectXTex DDS / WIC PNG encoders.
		void
		WritePng(const std::string& filepath, const assetlib::ImageData& image)
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

	RenderContext::RenderContext(
		DeviceRef          device,
		ResourceManagerRef resourceManager,
		bool               enableDebug) :
		m_Device(std::move(device)), m_ResourceManager(std::move(resourceManager)),
		m_EnableDebug(enableDebug)
	{
		// Registered so a deferred destroy cannot reclaim a slot this queue may still be reading.
		m_CommandQueue = m_Device->CreateGraphicsCommandQueue();
		m_ResourceManager->RegisterQueue(m_CommandQueue.Get());

		m_BootstrapAllocator = m_Device->CreateCommandAllocator();

		auto cmdListDesc = CommandListDesc();
		cmdListDesc.type = QueueType::kGraphics;
		m_CommandList =
			m_Device->CreateCommandList(cmdListDesc, m_BootstrapAllocator, m_ResourceManager);

		m_CompactInstances.Init(m_Device, m_ResourceManager);
		m_RigFrames.Init(m_Device);
		m_SkinnedPose.Init(m_Device);
		m_TransparentSort.Init(m_Device);
		m_Forward.Init(m_Device);
		m_Skybox.Init(m_Device);
		m_PostProcess.Init(m_Device);
		m_OverlayPass.Init(m_Device);
		m_OutlineMask.Init(m_Device);
		m_TaaResolve.Init(m_Device);

		m_PointClampSampler = m_ResourceManager->CreateSampler(
			SamplerDesc().SetAllFilters(false).SetAllAddressModes(SamplerAddressMode::kClamp));
		m_LinearClampSampler = m_ResourceManager->CreateSampler(
			SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));
		m_BrdfLut.Init(m_Device.Get(), m_ResourceManager);

		m_CommandList->Open(m_CommandQueue.Get(), m_BootstrapAllocator.Get());
		m_BrdfLut.Generate(m_CommandList.Get());
		m_CommandList->Close();
		m_CommandQueue->WaitForFenceCPUBlocking(m_CommandQueue->ExecuteCommandList(m_CommandList));

		m_BrdfLut.ReleaseTarget();

#if defined(BERNINI_GPU_DEBUG)
		m_BufferPoisoner.Init(m_ResourceManager);
		m_FrameGraph.SetBufferPoisoner(&m_BufferPoisoner);

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
		for (CaptureSlot& slot : m_Captures)
		{
			if (slot.ticketId != 0)
			{
				m_ResourceManager->DestroyReadbackBuffer(slot.readback, false);
			}
		}
		m_Forward.Release();
		m_Skybox.Release();
		m_PostProcess.Release();
		m_OverlayPass.Release();
		m_OutlineMask.Release();
		m_TaaResolve.Release();
		if (!m_PointClampSampler.IsNull())
		{
			m_ResourceManager->DestroySampler(m_PointClampSampler, false);
		}
		if (!m_LinearClampSampler.IsNull())
		{
			m_ResourceManager->DestroySampler(m_LinearClampSampler, false);
		}
		m_BrdfLut.Release();
		m_CompactInstances.Release(false);
		m_RigFrames.Release();
		m_SkinnedPose.Release();
		m_TransparentSort.Release();

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

		m_FrameGraph.SetBufferPoisoner(nullptr);
		m_BufferPoisoner.Release(false);
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

		// Not the caller's rt.GetFrameFence(index): that gates the caller's own last frame at this
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

	RenderTargetRef
	RenderContext::CreateRenderTarget(const RenderTargetDesc& desc)
	{
		return m_Device->CreateRenderTarget(desc, m_CommandQueue, m_ResourceManager, m_EnableDebug);
	}

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
		const uint32_t    index = rt.GetFrameIndex();

		uint64_t fenceToWaitOn = rt.GetFrameFence(index);
		if (fenceToWaitOn != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(fenceToWaitOn);
		}

#if defined(BERNINI_GPU_DEBUG)
		// This slot's fence has completed, so the GPU-assertion snapshot it copied out
		// (two frames ago) is now safe to read. Crashes if any assertion fired.
		InspectDebugSlot(index);
#endif

		rt.GetFrameAllocator(index)->ResetAllocator();

		m_CommandList->Open(m_CommandQueue.Get(), rt.GetFrameAllocator(index));

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
		m_DrawCount        = 0;
		m_TemporalBreak    = false;
		m_OutlineMaskDrawn = false;
		m_OverlayDraws.clear();
		m_FrameOverlays.clear();
		m_FrameSources.clear();
		++m_FrameCounter;
		rt.AdvanceFrameCount();
		m_FrameGraph.RegisterQueue("main", m_CommandQueue, m_CommandList);
		m_FrameGraph.ImportTexture(
			c_BackbufferName,
			rt.GetBackbufferTexture(index),
			AccessState{ BarrierSyncFlag::kNone,
		                 BarrierAccessFlag::kNone,
		                 BarrierLayout::kPresent });

		// Resumes the state the graph tracked last frame; the target creates them in
		// render-target / depth-write.
		m_FrameGraph.ImportTexture(c_MotionVectorsName, rt.GetMotionVectorTexture());
		m_FrameGraph.ImportTexture(c_SceneColorName, rt.GetSceneColorTexture());
		m_FrameGraph.ImportTexture(c_DepthName, rt.GetDepthTexture());
		m_FrameGraph.ImportTexture(c_OutlineMaskName, rt.GetOutlineMaskTexture());

		if (rt.IsTaaEnabled())
		{
			for (uint32_t i = 0; i < 2; ++i)
			{
				m_FrameGraph.ImportTexture(GetHistoryName(i), rt.GetHistoryTexture(i));
			}
		}

		// The backbuffer is not cleared: the tonemap covers it whole, and the overlay only ever
		// blends over what the tonemap wrote. Zero motion is "this pixel did not move", which is
		// what an untouched pixel should read as.
		const std::array<ClearPass::ColorTarget, 3> colorTargets{
			{ { std::string(c_SceneColorName), rt.GetSceneColorRtv(), { 0.0f, 0.0f, 0.0f, 1.0f } },
			  { std::string(c_MotionVectorsName),
			    rt.GetMotionVectorRtv(),
			    { 0.0f, 0.0f, 0.0f, 0.0f } },
			  { std::string(c_OutlineMaskName),
			    rt.GetOutlineMaskRtv(),
			    { 0.0f, 0.0f, 0.0f, 0.0f } } }
		};
		ClearPass().AttachToFrameGraph(
			m_FrameGraph,
			m_ResourceManager.Get(),
			colorTargets,
			std::string(c_DepthName),
			rt.GetDepthDsv());

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

		auto view  = job.view->As<SceneView>();
		auto scene = view->GetScene()->As<Scene>();

		// The job's viewport is output-space, because that is the frame a client can see. The
		// geometry passes are handed the render grid instead, and only the resolve spans both.
		const Viewport viewport = ToRenderViewport(*m_ActiveTarget, job.viewport);

		// The client's Camera never carries the jitter: TAA is a renderer concern, and a caller that
		// reads GetViewProjection() back -- to pick, or to project a gizmo -- must not get a matrix
		// that moves every frame. Indexed by the target's own frame count, not this context's: with
		// two targets drawn per frame each would otherwise see every second term of the sequence.
		const glm::vec2 jitter = m_ActiveTarget->IsTaaEnabled() ?
		                             HaltonJitter(
										 m_ActiveTarget->GetFrameCount(),
										 viewport.maxX - viewport.minX,
										 viewport.maxY - viewport.minY,
										 JitterSequenceLength(
											 m_ActiveTarget->GetRenderWidth(),
											 m_ActiveTarget->GetRenderHeight(),
											 m_ActiveTarget->GetWidth(),
											 m_ActiveTarget->GetHeight())) :
		                             glm::vec2(0.0f);

		// Left-multiplied, so it adds jitter * clip.w to clip.xy and lands as a constant NDC offset
		// after the divide. Applying it to the projection's own terms would be perspective-specific.
		const glm::mat4 projection =
			glm::translate(glm::mat4(1.0f), glm::vec3(jitter, 0.0f)) * job.camera.GetProjection();

		const auto viewProj = projection * job.camera.GetView();

		glm::mat4 viewNoTranslation = job.camera.GetView();
		viewNoTranslation[3]        = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		auto camera                 = ViewMatrices();
		camera.viewProj             = viewProj;
		camera.rotationOnlyViewProj = projection * viewNoTranslation;
		camera.jitter               = jitter;
		camera.unjitteredViewProj   = job.camera.GetProjection() * job.camera.GetView();
		camera.time                 = job.time;

		const ViewMatrices prevCamera = view->AdvanceCamera(m_FrameCounter, camera);

		m_TemporalBreak |= view->AdvanceTemporalEpoch();

		const glm::mat4 invView = glm::inverse(job.camera.GetView());

		// What the resolve tells a surface's own motion from the camera's with. One camera stands
		// for the target, so a frame of several draws disables it rather than choosing.
		m_TaaClipToView     = glm::inverse(job.camera.GetProjection());
		m_TaaViewToPrevClip = prevCamera.unjitteredViewProj * invView;
		m_TaaJitter         = jitter;

		const uint32_t drawIdx = m_DrawCount++;

		m_FrameGraph.SetResourceNamespace(view->GetResourceNamespace());

		scene->AttachToFrameGraph(m_FrameGraph, drawIdx);
		view->AttachToFrameGraph(m_FrameGraph, drawIdx);

		auto draw                         = DrawData();
		draw.drawIdx                      = drawIdx;
		draw.view                         = job.view;
		draw.cullIdx                      = c_CameraCullIdx;
		draw.cullState                    = &view->GetCullState(c_CameraCullIdx);
		draw.viewState.viewport           = viewport;
		draw.viewState.viewProj           = viewProj;
		draw.viewState.prevViewProj       = prevCamera.viewProj;
		draw.viewState.jitter             = jitter;
		draw.viewState.prevJitter         = prevCamera.jitter;
		draw.clock.time                   = job.time;
		draw.clock.prevTime               = prevCamera.time;
		draw.viewState.cullView           = BuildCullView(viewProj);
		draw.viewState.unjitteredViewProj = camera.unjitteredViewProj;
		draw.targets.sceneColor           = m_ActiveTarget->GetSceneColorRtv();
		draw.targets.depth                = m_ActiveTarget->GetDepthDsv();
		draw.targets.motionVector         = m_ActiveTarget->GetMotionVectorRtv();
		draw.targets.outlineMask          = m_ActiveTarget->GetOutlineMaskRtv();

		draw.materialArena            = scene->GetMaterialBinding();
		draw.samplers.anisoLinearWrap = scene->GetSampler(Scene::StandardSampler::kAnisoLinearWrap);
		draw.samplers.linearClamp     = scene->GetSampler(Scene::StandardSampler::kLinearClamp);

		draw.viewState.cameraPos = glm::vec3(invView[3]);

		draw.lighting.env         = view->GetEnvironmentMap();
		draw.lighting.env.brdfLut = m_BrdfLut.GetSrv();
		draw.lighting.exposure    = view->GetExposure();

		// Still without temporal AA: a coverage pattern nothing accumulates is flicker. Its period is
		// not the jitter's -- eight patterns average to nine grey levels rather than to coverage.
		constexpr uint64_t c_AlphaHashPeriod = 1024;

		draw.viewState.alphaHashSeed =
			m_ActiveTarget->IsTaaEnabled() ?
				static_cast<float>(m_ActiveTarget->GetFrameCount() % c_AlphaHashPeriod) :
				0.0f;
		draw.lighting.skybox = view->GetSkybox();

		if (draw.lighting.skybox.has_value())
		{
			const float rotationY = draw.lighting.skybox->rotationY;

			draw.lighting.envRotation = glm::vec2(std::sin(rotationY), std::cos(rotationY));

			auto skyRotation = glm::mat4(1.0f);
			if (rotationY != 0.0f)
			{
				skyRotation = glm::rotate(glm::mat4(1.0f), rotationY, glm::vec3(0.0f, 1.0f, 0.0f));
			}

			// Composed from the pieces, the jitter as an exact translation: inverting their product
			// folds the jitter into the rotation and a still sky reports motion (docs/passes.md).
			draw.lighting.skyboxClipToWorld =
				skyRotation * glm::transpose(viewNoTranslation) *
				glm::inverse(job.camera.GetProjection()) *
				glm::translate(glm::mat4(1.0f), glm::vec3(-jitter, 0.0f));

			// Undoes the spin the ray direction was baked with before reprojecting, so a rotated
			// skybox reports the camera's motion and not its own offset. rotationY is authoring
			// state rather than per-frame animation, so last frame's spin is taken to be this one's.
			draw.lighting.skyboxPrevWorldToClip =
				prevCamera.rotationOnlyViewProj * glm::inverse(skyRotation);

			m_Skybox.AttachToFrameGraph(m_FrameGraph, draw);
		}

		// A palette is per instance, not per frustum, so posing runs once for the view rather than once
		// per cull -- and it must be attached under the view's namespace, where its output buffer was
		// imported. Under a cull namespace the write would resolve to a name nothing imported, which
		// makes the pass no longer a root and culls it.
		// Before the pose pass and the forward pass, both of which may read a table filled here.
		m_RigFrames.AttachToFrameGraph(m_FrameGraph, draw);
		m_SkinnedPose.AttachToFrameGraph(m_FrameGraph, draw);

		// The skybox above names only globals; everything below reads cull outputs.
		m_FrameGraph.SetResourceNamespace(view->GetCullNamespace(draw.cullIdx));

		// Cull first (a sub-pass of CompactInstances writes the visibility word), then the transparent
		// sort, which reads it.
		m_CompactInstances.AttachToFrameGraph(m_FrameGraph, draw);
		m_TransparentSort.AttachToFrameGraph(m_FrameGraph, draw);
		m_Forward.AttachToFrameGraph(m_FrameGraph, draw);

		if (const auto selected = view->GetSelectedInstances();
		    !selected.empty() && m_ActiveTarget->IsOutlineEnabled())
		{
			m_OutlineMask.AttachToFrameGraph(
				m_FrameGraph,
				draw,
				static_cast<uint32_t>(selected.size()));
			m_OutlineMaskDrawn = true;
		}
	}

	void
	RenderContext::DrawOverlay(const OverlayJob& job)
	{
		if (!m_FrameActive)
		{
			throw GraphicsError("DrawOverlay must be called between BeginFrame and EndFrame");
		}

		if (job.overlay == nullptr)
		{
			throw GraphicsError("OverlayJob passed to DrawOverlay requires an overlay");
		}

		auto* overlay = job.overlay->As<Overlay>();
		gassert(overlay != nullptr, "An IOverlay this graphics did not create");

		// Every draw is checked before any is queued, so a bad one leaves the frame as it was.
		for (const OverlayDraw& draw : job.draws)
		{
			if (!overlay->ValidGeometry(draw.geometry))
			{
				throw GraphicsError(
					"OverlayDraw names a geometry that is null, released, or not this overlay's");
			}

			if (draw.texture.IsValid() && !overlay->ValidTexture(draw.texture))
			{
				throw GraphicsError(
					"OverlayDraw names a texture that is released or not this overlay's");
			}

			if (overlay->GetTextureTarget(draw.texture) == m_ActiveTarget)
			{
				throw GraphicsError(
					"OverlayDraw samples the target this frame draws to; a target's output is "
					"drawn on another target");
			}

			if (draw.scissor.has_value() && (draw.scissor->width < 0 || draw.scissor->height < 0))
			{
				throw GraphicsError("OverlayDraw scissor has a negative extent");
			}
		}

		const bool overlayHeld = std::ranges::any_of(m_FrameOverlays, [&](const auto& held) {
			return held.Get() == overlay;
		});

		if (!overlayHeld)
		{
			m_FrameOverlays.push_back(core::SharedRef<Overlay>(overlay));
		}

		const auto targetRect = Rect(Viewport(
			static_cast<float>(m_ActiveTarget->GetWidth()),
			static_cast<float>(m_ActiveTarget->GetHeight())));

		for (const OverlayDraw& draw : job.draws)
		{
			const OverlayGeometry& geometry = overlay->GetGeometry(draw.geometry);

			auto resolved          = OverlayPass::Draw();
			resolved.vertices      = geometry.vertices;
			resolved.indices       = geometry.indices;
			resolved.triangleCount = geometry.triangleCount;
			resolved.texture       = overlay->GetTextureSrv(draw.texture);
			resolved.translation   = draw.translation;

			// A target that has not presented resolved to white above, so its ring is not read
			// and is not imported.
			if (RenderTargetBase* source = overlay->GetTextureTarget(draw.texture);
			    source != nullptr && source->HasPresented())
			{
				const bool held = std::ranges::any_of(m_FrameSources, [&](const auto& s) {
					return s.Get() == source;
				});

				if (!held)
				{
					m_FrameSources.push_back(core::SharedRef<RenderTargetBase>(source));
				}
			}
			resolved.transform = draw.transform.value_or(glm::mat4(1.0f));
			resolved.scissor   = targetRect;

			if (draw.scissor.has_value())
			{
				// Summed in 64 bits: a client's rectangle may sit anywhere in int range, and the
				// clamp is what brings it back onto the target.
				const OverlayRect& s      = *draw.scissor;
				const auto         clampX = [&](int64_t v) {
					return static_cast<int>(
						std::clamp<int64_t>(v, targetRect.minX, targetRect.maxX));
				};
				const auto clampY = [&](int64_t v) {
					return static_cast<int>(
						std::clamp<int64_t>(v, targetRect.minY, targetRect.maxY));
				};
				resolved.scissor.minX = clampX(s.x);
				resolved.scissor.minY = clampY(s.y);
				resolved.scissor.maxX = clampX(static_cast<int64_t>(s.x) + s.width);
				resolved.scissor.maxY = clampY(static_cast<int64_t>(s.y) + s.height);
			}

			m_OverlayDraws.push_back(resolved);
		}
	}

	void
	RenderContext::EndFrame()
	{
		if (!m_FrameActive)
		{
			throw GraphicsError("EndFrame called without a matching BeginFrame");
		}

		RenderTargetBase& rt    = *m_ActiveTarget;
		const uint32_t    index = rt.GetFrameIndex();

		m_FrameGraph.SetResourceNamespace("");

		const auto viewport =
			Viewport(static_cast<float>(rt.GetWidth()), static_cast<float>(rt.GetHeight()));

		const auto renderSize = glm::vec2(
			static_cast<float>(rt.GetRenderWidth()),
			static_cast<float>(rt.GetRenderHeight()));

		auto postProcessArgs       = PostProcessPass::Args();
		postProcessArgs.source     = rt.GetSceneColorSrv();
		postProcessArgs.sourceName = std::string(c_SceneColorName);
		postProcessArgs.backBuffer = rt.GetBackbufferRtv(index);
		postProcessArgs.viewport   = viewport;

		// With the resolve running, the source is the history and already on this viewport's grid,
		// so a point tap is what keeps it exact. Without it, the scene colour arrives on the render
		// grid and something has to carry it across.
		const bool sourceOnOutputGrid =
			rt.IsTaaEnabled() ||
			(rt.GetRenderWidth() == rt.GetWidth() && rt.GetRenderHeight() == rt.GetHeight());

		postProcessArgs.sampler = sourceOnOutputGrid ? m_PointClampSampler : m_LinearClampSampler;
		postProcessArgs.maskSampler = m_PointClampSampler;

		if (m_OutlineMaskDrawn)
		{
			postProcessArgs.outlineMask    = rt.GetOutlineMaskSrv();
			postProcessArgs.outlineEnabled = true;
			// The mask's own grid, which is the render one: the dilate walks its texels and the
			// outline's width is a share of the frame either way.
			postProcessArgs.maskSize = glm::vec2(
				static_cast<float>(rt.GetRenderWidth()),
				static_cast<float>(rt.GetRenderHeight()));
		}

		if (rt.IsTaaEnabled())
		{
			const uint32_t current = rt.GetCurrentHistoryIndex();
			const uint32_t prev    = current ^ 1u;

			auto taaArgs                = TaaResolvePass::Args();
			taaArgs.sceneColor          = rt.GetSceneColorSrv();
			taaArgs.motionVectors       = rt.GetMotionVectorSrv();
			taaArgs.prevHistory         = rt.GetHistorySrv(prev);
			taaArgs.history             = rt.GetHistoryRtv(current);
			taaArgs.prevHistoryName     = GetHistoryName(prev);
			taaArgs.historyName         = GetHistoryName(current);
			taaArgs.pointSampler        = m_PointClampSampler;
			taaArgs.linearSampler       = m_LinearClampSampler;
			taaArgs.viewport            = viewport;
			taaArgs.renderSize          = renderSize;
			taaArgs.reconstructionWidth = rt.GetTaaReconstructionWidth();
			taaArgs.depth               = rt.GetDepthSrv();
			taaArgs.clipToView          = m_TaaClipToView;
			taaArgs.viewToPrevClip      = m_TaaViewToPrevClip;
			taaArgs.jitter              = m_TaaJitter;
			taaArgs.cameraPairValid     = m_DrawCount == 1;
			taaArgs.historyValid        = rt.IsHistoryValid() && !m_TemporalBreak;
			m_TaaResolve.AttachToFrameGraph(m_FrameGraph, taaArgs);

			// The display curve is applied to what the resolve produced, not to the raw frame.
			postProcessArgs.source     = rt.GetHistorySrv(current);
			postProcessArgs.sourceName = GetHistoryName(current);
		}

		m_PostProcess.AttachToFrameGraph(m_FrameGraph, postProcessArgs);

		// Every presentable this frame leaves in kPresent: its own backbuffer first.
		std::vector<std::string> presentables{ std::string(c_BackbufferName) };

		if (!m_OverlayDraws.empty())
		{
			// A borrowed backbuffer is imported under its own name with an explicit present
			// initial: the handle behind the name changes as that target's ring advances, so a
			// state resumed from an earlier frame would describe another slot.
			std::vector<std::string> sources;
			sources.reserve(m_FrameSources.size());

			for (const core::SharedRef<RenderTargetBase>& source : m_FrameSources)
			{
				sources.push_back(std::format("overlay_source_{}", sources.size()));
				m_FrameGraph.ImportTexture(
					sources.back(),
					source->GetBackbufferTexture(source->GetLastPresentedIndex()),
					AccessState{ BarrierSyncFlag::kNone,
				                 BarrierAccessFlag::kNone,
				                 BarrierLayout::kPresent });
			}

			auto overlayArgs       = OverlayPass::Args();
			overlayArgs.overlays   = m_FrameOverlays;
			overlayArgs.draws      = m_OverlayDraws;
			overlayArgs.backBuffer = rt.GetBackbufferRtv(index);
			overlayArgs.viewport   = viewport;
			overlayArgs.sampler    = m_LinearClampSampler;
			m_OverlayPass.AttachToFrameGraph(m_FrameGraph, overlayArgs, sources);

			presentables.insert(presentables.end(), sources.begin(), sources.end());
		}

		m_PreparePresentPass.AttachToFrameGraph(m_FrameGraph, presentables);

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

		if (rt.IsTaaEnabled())
		{
			rt.AdvanceHistory();
		}

		rt.PresentAndAdvance();

		m_ResourceManager->CleanupExpiredResources();

		m_OverlayDraws.clear();
		m_FrameOverlays.clear();
		m_FrameSources.clear();

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

	void
	RenderContext::SetRenderScale(const RenderTargetRef& target, float scale)
	{
		if (m_FrameActive)
		{
			throw GraphicsError("SetRenderScale cannot be called between BeginFrame and EndFrame");
		}

		RenderTargetBase& rt = *target->As<RenderTargetBase>();

		if (rt.GetRenderScale() == scale)
		{
			return;
		}

		// The same idle a resize needs: the attachments about to be released may still be referenced
		// by a frame in flight, and by the command list's own retained references.
		m_CommandQueue->Flush();

		m_BootstrapAllocator->ResetAllocator();
		m_CommandList->Open(m_CommandQueue.Get(), m_BootstrapAllocator.Get());
		m_CommandList->Close();

		rt.SetRenderScale(scale);
	}

	CaptureTicket
	RenderContext::SubmitCapture(const RenderTargetRef& target)
	{
		return SubmitCaptureImpl(target, "SubmitCapture");
	}

	CaptureTicket
	RenderContext::SubmitCaptureImpl(const RenderTargetRef& target, std::string_view caller)
	{
		if (m_FrameActive)
		{
			throw GraphicsError(
				std::format("{} cannot be called between BeginFrame and EndFrame", caller));
		}

		const auto free = std::ranges::find_if(m_Captures, [](const CaptureSlot& slot) {
			return slot.ticketId == 0;
		});
		if (free == m_Captures.end())
		{
			throw GraphicsError(
				std::format(
					"{}: all {} capture slots are in flight; resolve or discard one first",
					caller,
					IGraphics::c_MaxPendingCaptures));
		}
		CaptureSlot& slot = *free;

		RenderTargetBase& rt = *target->As<RenderTargetBase>();

		const uint32_t index         = rt.GetLastPresentedIndex();
		TextureHandle  textureHandle = rt.GetBackbufferTexture(index);

		// A discard frees the slot with its copy possibly still in flight; the allocator cannot
		// be reset under it.
		if (slot.fence != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(slot.fence);
		}

		if (slot.allocator == nullptr)
		{
			slot.allocator = m_Device->CreateCommandAllocator();
		}
		slot.allocator->ResetAllocator();

		slot.layout = m_ResourceManager->GetTextureReadbackLayout(textureHandle);

		auto readbackDesc      = ReadbackBufferDesc();
		readbackDesc.byteSize  = slot.layout.totalBytes;
		readbackDesc.debugName = "Capture Readback";
		slot.readback          = m_ResourceManager->CreateReadbackBuffer(readbackDesc);

		m_CommandList->Open(m_CommandQueue.Get(), slot.allocator.Get());

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

		m_CommandList->CopyTextureToReadback(slot.readback, textureHandle);

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

		// No wait on the producing frame's fence: one queue executes in submission order, so the
		// copy runs after the frame that filled this backbuffer.
		slot.fence = m_CommandQueue->ExecuteCommandList(m_CommandList);

		const TextureDesc texDesc = m_ResourceManager->GetTextureDesc(textureHandle);
		slot.width                = texDesc.width;
		slot.height               = texDesc.height;
		slot.format               = texDesc.format;

		slot.ticketId = m_NextCaptureId++;
		return CaptureTicket{ slot.ticketId };
	}

	RenderContext::CaptureSlot&
	RenderContext::FindCapture(CaptureTicket ticket)
	{
		if (ticket.IsValid())
		{
			for (CaptureSlot& slot : m_Captures)
			{
				if (slot.ticketId == ticket.id)
				{
					return slot;
				}
			}
		}
		throw GraphicsError("Capture ticket is null, already resolved, or discarded");
	}

	std::optional<assetlib::ImageData>
	RenderContext::TryResolveCapture(CaptureTicket ticket)
	{
		CaptureSlot& slot = FindCapture(ticket);

		if (!m_CommandQueue->IsFenceComplete(slot.fence))
		{
			return std::nullopt;
		}

		const void* mapped = m_ResourceManager->MapReadback(slot.readback);

		auto image = std::optional<assetlib::ImageData>();
		try
		{
			image = ReadbackToImage(
				static_cast<const uint8_t*>(mapped) + slot.layout.offset,
				static_cast<size_t>(slot.layout.rowPitch),
				slot.width,
				slot.height,
				slot.format);
		}
		catch (...)
		{
			m_ResourceManager->UnmapReadback(slot.readback);
			m_ResourceManager->DestroyReadbackBuffer(slot.readback, false);
			slot.readback = {};
			slot.ticketId = 0;
			throw;
		}

		m_ResourceManager->UnmapReadback(slot.readback);
		m_ResourceManager->DestroyReadbackBuffer(slot.readback, false);
		slot.readback = {};
		slot.ticketId = 0;
		return image;
	}

	void
	RenderContext::DiscardCapture(CaptureTicket ticket) noexcept
	{
		if (!ticket.IsValid())
		{
			return;
		}

		for (CaptureSlot& slot : m_Captures)
		{
			if (slot.ticketId == ticket.id)
			{
				// The copy may still be in flight: the deferred destroy gates on this queue, and
				// slot.fence stays recorded so the next submit here waits before resetting the
				// allocator.
				m_ResourceManager->DestroyReadbackBuffer(slot.readback, true);
				slot.readback = {};
				slot.ticketId = 0;
				return;
			}
		}
	}

	// The last presented backbuffer of `target`, read back into a tight RGBA8 image. One blocking
	// wait, on the copy's fence -- which also covers the producing frame, queued before it.
	assetlib::ImageData
	RenderContext::CaptureBackbuffer(const RenderTargetRef& target, std::string_view caller)
	{
		const CaptureTicket ticket = SubmitCaptureImpl(target, caller);
		m_CommandQueue->WaitForFenceCPUBlocking(FindCapture(ticket).fence);

		auto image = TryResolveCapture(ticket);
		gassert(image.has_value(), "Capture fence completed but the resolve returned no image");
		return std::move(*image);
	}

	void
	RenderContext::ScreenshotPng(const RenderTargetRef& target, const std::string& filepath)
	{
		WritePng(filepath, CaptureBackbuffer(target, "ScreenshotPng"));
	}

	assetlib::ImageData
	RenderContext::ScreenshotToMemory(const RenderTargetRef& target)
	{
		return CaptureBackbuffer(target, "ScreenshotToMemory");
	}
}
