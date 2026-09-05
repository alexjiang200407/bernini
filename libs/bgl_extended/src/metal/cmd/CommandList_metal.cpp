#include "cmd/CommandList_metal.h"

#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_metal.h"
#include "cmd/TimestampHeap.h"
#include "cmd/TimestampHeap_metal.h"
#include "constants/constants.h"
#include "convert_metal.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline_metal.h"
#include "pipeline/MeshletKernel.h"
#include "pipeline/MeshletPipeline_metal.h"
#include "pipeline/MetalPipelineReflection.h"
#include "resource/Buffer.h"
#include "resource/FrameBuffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/ResourceManager_metal.h"
#include "resource/Texture.h"
#include "types/ComputeState.h"
#include "types/MeshletState.h"
#include "types/Rect.h"
#include "types/RenderState.h"
#include "types/ShaderStage.h"
#include "uniforms/UniformLayoutEntry.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <bgl/Viewport.h>
#include <bgl_common/ReflectedLayout.h>
#include <bgl_common/gassert.h>
#include <core/containers/slot_handle.h>

#include <algorithm>
#include <bgl_common/idl/DispatchArgs.h>

#include <core/math.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		struct MappedUniform
		{
			std::vector<std::byte>      bytes;
			std::vector<MTL::Resource*> resident;  // resources the encoder must declare
		};

		// D3D12 writes a bindless handle's slot index into the cbuffer and a directly-indexed heap
		// resolves it in-shader; Metal has no such heap, so at dispatch each handle field is rewritten
		// to the native value the emitted MSL dereferences -- a device address for a buffer, an
		// MTLResourceID for a texture or sampler. They come from different pools, which is why the
		// kind travels alongside the offset. Returns the resources to declare. Compute and mesh.
		MappedUniform
		MapUniformHandlesToGpuAddresses(
			const Uniforms&                uniforms,
			const std::vector<HandleSlot>& handles,
			ResourceManager*               rm)
		{
			MappedUniform result;
			const size_t  size = uniforms.GetSize();
			result.bytes.resize(size);
			std::memcpy(result.bytes.data(), uniforms.Data(), size);

			for (const HandleSlot& handle : handles)
			{
				uint32_t slotIndex = 0;
				std::memcpy(&slotIndex, result.bytes.data() + handle.offset, sizeof(uint32_t));

				// An optional binding the CPU left unset -- either assigned a null handle, or never
				// assigned at all, which leaves the zero-filled mirror reading the reserved sentinel.
				// Both stay a null id: the rewrite would otherwise dereference a pool slot that does
				// not exist, or resolve the sentinel to whatever sits at index 0.
				if (slotIndex == core::slot_handle::invalid_index ||
				    slotIndex == c_UnboundDescriptorIndex)
				{
					constexpr uint64_t c_NullId = 0;
					std::memcpy(result.bytes.data() + handle.offset, &c_NullId, sizeof(uint64_t));
					continue;
				}

				uint64_t native = 0;
				switch (handle.kind)
				{
				case HandleKind::kBuffer:
				{
					MTL::Buffer* buffer = rm->GetBufferBySlotIndex(slotIndex);
					native              = buffer->gpuAddress();
					result.resident.push_back(buffer);
					break;
				}
				case HandleKind::kTexture:
				{
					MTL::Texture* texture = rm->GetTextureBySlotIndex(slotIndex);
					native                = texture->gpuResourceID()._impl;
					result.resident.push_back(texture);
					break;
				}
				case HandleKind::kSampler:
					// A sampler is not a MTL::Resource and needs no residency.
					native = rm->GetSamplerBySlotIndex(slotIndex)->gpuResourceID()._impl;
					break;
				case HandleKind::kNone:
					gfatal("A collected handle slot carries no kind");
				}
				std::memcpy(result.bytes.data() + handle.offset, &native, sizeof(uint64_t));
			}
			return result;
		}
	}

	CommandList::CommandList(
		const CommandListDesc& desc,
		ICommandAllocator*,
		ResourceManagerRef resourceManager) :
		m_Desc(desc), m_ResourceManager(std::move(resourceManager))
	{
		gassert(m_ResourceManager != nullptr, "Resource manager cannot be null");
		m_Device = m_ResourceManager->As<ResourceManager>()->GetMTLDevice();
	}

	namespace
	{
		bool
		SameAttachments(const FrameBuffer& a, const FrameBuffer& b) noexcept
		{
			if (a.colorAttachments.size() != b.colorAttachments.size())
				return false;
			for (size_t i = 0; i < a.colorAttachments.size(); ++i)
			{
				if (a.colorAttachments[i].idx != b.colorAttachments[i].idx ||
				    a.colorAttachments[i].generation != b.colorAttachments[i].generation)
					return false;
			}
			return a.depthAttachment.idx == b.depthAttachment.idx &&
			       a.depthAttachment.generation == b.depthAttachment.generation;
		}
	}

	void
	CommandList::EndEncoder() noexcept
	{
		if (m_Encoder == nullptr)
			return;
		m_Encoder->endEncoding();
		m_Encoder     = nullptr;
		m_EncoderKind = EncoderKind::kNone;
	}

	MTL::BlitCommandEncoder*
	CommandList::GetBlitEncoder() noexcept
	{
		if (m_EncoderKind != EncoderKind::kBlit)
		{
			EndEncoder();
			if (m_TimingBuffer != nullptr)
			{
				MTL::BlitPassDescriptor* pass = MTL::BlitPassDescriptor::blitPassDescriptor();
				AttachTiming(pass);
				m_Encoder = m_CmdBuffer->blitCommandEncoder(pass);
			}
			else
			{
				m_Encoder = m_CmdBuffer->blitCommandEncoder();
			}
			m_EncoderKind = EncoderKind::kBlit;
		}
		return static_cast<MTL::BlitCommandEncoder*>(m_Encoder);
	}

	MTL::ComputeCommandEncoder*
	CommandList::GetComputeEncoder() noexcept
	{
		if (m_EncoderKind != EncoderKind::kCompute)
		{
			EndEncoder();
			if (m_TimingBuffer != nullptr)
			{
				MTL::ComputePassDescriptor* pass =
					MTL::ComputePassDescriptor::computePassDescriptor();
				AttachTiming(pass);
				m_Encoder = m_CmdBuffer->computeCommandEncoder(pass);
			}
			else
			{
				m_Encoder = m_CmdBuffer->computeCommandEncoder();
			}
			m_EncoderKind = EncoderKind::kCompute;
		}
		return static_cast<MTL::ComputeCommandEncoder*>(m_Encoder);
	}

	MTL::RenderCommandEncoder*
	CommandList::RenderEncoder(const FrameBuffer& fb) noexcept
	{
		if (m_EncoderKind == EncoderKind::kRender && SameAttachments(m_EncoderFrameBuffer, fb))
			return static_cast<MTL::RenderCommandEncoder*>(m_Encoder);

		EndEncoder();

		auto* rm = m_ResourceManager->As<ResourceManager>();

		// Load-preserve: a clear is its own pass, so whatever it wrote survives into this one.
		MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
		for (size_t i = 0; i < fb.colorAttachments.size(); ++i)
		{
			MTL::Texture* texture =
				rm->GetTexture(rm->GetRtvTexture(fb.colorAttachments[i])).GetMTLResource();
			MTL::RenderPassColorAttachmentDescriptor* c = pass->colorAttachments()->object(i);
			c->setTexture(texture);
			c->setLoadAction(MTL::LoadActionLoad);
			c->setStoreAction(MTL::StoreActionStore);
		}

		if (!fb.depthAttachment.IsNull())
		{
			const TextureHandle texHandle = rm->GetDsvTexture(fb.depthAttachment);
			MTL::Texture*       texture   = rm->GetTexture(texHandle).GetMTLResource();

			MTL::RenderPassDepthAttachmentDescriptor* d = pass->depthAttachment();
			d->setTexture(texture);
			d->setLoadAction(MTL::LoadActionLoad);
			d->setStoreAction(MTL::StoreActionStore);

			// One texture, two attachments: Metal binds the stencil plane separately from depth.
			if (GetFormatInfo(rm->GetTexture(texHandle).GetDesc().format).hasStencil)
			{
				MTL::RenderPassStencilAttachmentDescriptor* s = pass->stencilAttachment();
				s->setTexture(texture);
				s->setLoadAction(MTL::LoadActionLoad);
				s->setStoreAction(MTL::StoreActionStore);
			}
		}

		AttachTiming(pass);
		m_Encoder            = m_CmdBuffer->renderCommandEncoder(pass);
		m_EncoderKind        = EncoderKind::kRender;
		m_EncoderFrameBuffer = fb;
		return static_cast<MTL::RenderCommandEncoder*>(m_Encoder);
	}

	void
	CommandList::AttachTiming(MTL::RenderPassDescriptor* pass) noexcept
	{
		if (m_TimingBuffer == nullptr)
			return;

		MTL::RenderPassSampleBufferAttachmentDescriptor* a =
			pass->sampleBufferAttachments()->object(0);
		a->setSampleBuffer(m_TimingBuffer);
		a->setStartOfVertexSampleIndex(
			m_TimingStartSampled ? MTL::CounterDontSample : m_TimingStartSlot);
		a->setEndOfVertexSampleIndex(MTL::CounterDontSample);
		a->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
		a->setEndOfFragmentSampleIndex(m_TimingEndSlot);
		m_TimingStartSampled = true;
	}

	void
	CommandList::AttachTiming(MTL::ComputePassDescriptor* pass) noexcept
	{
		if (m_TimingBuffer == nullptr)
			return;

		MTL::ComputePassSampleBufferAttachmentDescriptor* a =
			pass->sampleBufferAttachments()->object(0);
		a->setSampleBuffer(m_TimingBuffer);
		a->setStartOfEncoderSampleIndex(
			m_TimingStartSampled ? MTL::CounterDontSample : m_TimingStartSlot);
		a->setEndOfEncoderSampleIndex(m_TimingEndSlot);
		m_TimingStartSampled = true;
	}

	void
	CommandList::AttachTiming(MTL::BlitPassDescriptor* pass) noexcept
	{
		if (m_TimingBuffer == nullptr)
			return;

		MTL::BlitPassSampleBufferAttachmentDescriptor* a =
			pass->sampleBufferAttachments()->object(0);
		a->setSampleBuffer(m_TimingBuffer);
		a->setStartOfEncoderSampleIndex(
			m_TimingStartSampled ? MTL::CounterDontSample : m_TimingStartSlot);
		a->setEndOfEncoderSampleIndex(m_TimingEndSlot);
		m_TimingStartSampled = true;
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator*) noexcept
	{
		gassert(cmdQueue != nullptr, "Command queue cannot be null");
		gassert(!m_Open, "Command list is already open");

		// One pool for the whole open scope: every encoder and temporary autoreleased between Open and
		// Close drains here. The command buffer outlives the drain via its own retain (RetainPtr).
		m_ScopePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		auto* queue = cmdQueue->As<CommandQueue>();
		m_CmdBuffer = NS::RetainPtr(queue->NewCommandBuffer());

		// Before any encoder: a wait encoded after them would sit past the work it must gate.
		queue->BeginCommandBuffer(m_CmdBuffer.get());

		m_Encoder     = nullptr;
		m_EncoderKind = EncoderKind::kNone;
		m_Open        = true;
	}

	void
	CommandList::Close() noexcept
	{
		gassert(m_Open, "Command list is not open");
		gassert(m_TimingBuffer == nullptr, "Command list closed with a timed span open");
		EndEncoder();
		m_Open = false;
		m_ScopePool.reset();
	}

	void
	CommandList::WriteBuffer(
		BufferHandle handle,
		const void*  data,
		size_t       gpuBufferOffset,
		size_t       byteSize) noexcept
	{
		gassert(m_Open, "WriteBuffer on a closed command list");
		gassert(m_ResourceManager->ValidBufferHandle(handle), "WriteBuffer on an invalid handle");

		auto* dst = m_ResourceManager->GetBuffer(handle).GetMTLResource();

		// A private buffer cannot be written from the CPU; stage the bytes in a shared buffer and blit
		// them across on the GPU timeline, so the write orders ahead of a later readback copy. The
		// command buffer retains the staging buffer until it completes, so it needs no separate owner.
		auto staging =
			NS::TransferPtr(m_Device->newBuffer(data, byteSize, MTL::ResourceStorageModeShared));
		gassert(staging.get() != nullptr, "Metal upload staging buffer allocation failed");

		GetBlitEncoder()->copyFromBuffer(staging.get(), 0, dst, gpuBufferOffset, byteSize);
	}

	void
	CommandList::CopyBuffer(
		BufferHandle dst,
		BufferHandle src,
		uint64_t     dstOffset,
		uint64_t     srcOffset,
		uint64_t     byteSize) noexcept
	{
		gassert(m_Open, "CopyBuffer on a closed command list");
		gassert(m_ResourceManager->ValidBufferHandle(src), "CopyBuffer: invalid source");
		gassert(m_ResourceManager->ValidBufferHandle(dst), "CopyBuffer: invalid destination");

		auto* srcBuffer = m_ResourceManager->GetBuffer(src).GetMTLResource();
		auto* dstBuffer = m_ResourceManager->GetBuffer(dst).GetMTLResource();

		GetBlitEncoder()->copyFromBuffer(srcBuffer, srcOffset, dstBuffer, dstOffset, byteSize);
	}

	void
	CommandList::CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept
	{
		gassert(m_Open, "CopyBufferToReadback on a closed command list");
		gassert(m_ResourceManager->ValidBufferHandle(src), "CopyBufferToReadback: invalid source");
		gassert(
			m_ResourceManager->ValidReadbackBufferHandle(dst),
			"CopyBufferToReadback: invalid destination");

		const auto& srcBuffer = m_ResourceManager->GetBuffer(src);
		auto*       dstBuffer = m_ResourceManager->GetReadbackBuffer(dst).GetMTLResource();

		GetBlitEncoder()->copyFromBuffer(
			srcBuffer.GetMTLResource(),
			0,
			dstBuffer,
			0,
			srcBuffer.GetDesc().byteSize);
	}

	void
	CommandList::CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept
	{
		gassert(m_Open, "CopyTextureToReadback on a closed command list");
		gassert(
			m_ResourceManager->ValidTextureHandle(src),
			"CopyTextureToReadback: invalid source");
		gassert(
			m_ResourceManager->ValidReadbackBufferHandle(dst),
			"CopyTextureToReadback: invalid destination");

		const Texture&              tex    = m_ResourceManager->GetTexture(src);
		const TextureDesc&          desc   = tex.GetDesc();
		const TextureReadbackLayout layout = m_ResourceManager->GetTextureReadbackLayout(src);
		MTL::Buffer* dstBuffer = m_ResourceManager->GetReadbackBuffer(dst).GetMTLResource();

		GetBlitEncoder()->copyFromTexture(
			tex.GetMTLResource(),
			0,
			0,
			MTL::Origin(0, 0, 0),
			MTL::Size(desc.width, desc.height, 1),
			dstBuffer,
			layout.offset,
			layout.rowPitch,
			layout.totalBytes);
	}

	void
	CommandList::ClearRenderTarget(MTL::Texture* texture, const float clearVal[4]) noexcept
	{
		gassert(m_Open, "ClearRenderTarget on a closed command list");
		EndEncoder();

		MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
		MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
		color->setTexture(texture);
		color->setLoadAction(MTL::LoadActionClear);
		color->setStoreAction(MTL::StoreActionStore);
		color->setClearColor(
			MTL::ClearColor::Make(clearVal[0], clearVal[1], clearVal[2], clearVal[3]));

		// An empty pass: the Clear load action writes the color and Store keeps it.
		AttachTiming(pass);
		m_CmdBuffer->renderCommandEncoder(pass)->endEncoding();
	}

	void
	CommandList::ClearDepthStencil(MTL::Texture* texture, float depth, uint8_t stencil) noexcept
	{
		gassert(m_Open, "ClearDepthStencil on a closed command list");
		EndEncoder();

		MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();

		MTL::RenderPassDepthAttachmentDescriptor* d = pass->depthAttachment();
		d->setTexture(texture);
		d->setLoadAction(MTL::LoadActionClear);
		d->setStoreAction(MTL::StoreActionStore);
		d->setClearDepth(depth);

		// Only a combined format carries stencil; attaching it to a depth-only texture is an error.
		if (texture->pixelFormat() == MTL::PixelFormatDepth32Float_Stencil8)
		{
			MTL::RenderPassStencilAttachmentDescriptor* st = pass->stencilAttachment();
			st->setTexture(texture);
			st->setLoadAction(MTL::LoadActionClear);
			st->setStoreAction(MTL::StoreActionStore);
			st->setClearStencil(stencil);
		}

		AttachTiming(pass);
		m_CmdBuffer->renderCommandEncoder(pass)->endEncoding();
	}

	void
	CommandList::WriteTexture(
		TextureHandle                           handle,
		std::span<const TextureSubresourceData> subresources) noexcept
	{
		gassert(m_Open, "WriteTexture on a closed command list");
		gassert(m_ResourceManager->ValidTextureHandle(handle), "WriteTexture on an invalid handle");

		const Texture&     texture = m_ResourceManager->GetTexture(handle);
		const TextureDesc& desc    = texture.GetDesc();
		MTL::Texture*      dst     = texture.GetMTLResource();

		// Block-aware, not per-pixel: BC5 and BC7 carry 16 bytes per 4x4 block, so a row is
		// ceil(width/4) blocks and a "row" of the source covers four texel rows.
		const FormatInfo format = GetFormatInfo(desc.format);

		// One staging buffer per subresource, blitted on the GPU timeline so the upload orders ahead
		// of whatever samples it. The command buffer retains each until it completes.
		auto* blit = GetBlitEncoder();
		for (size_t i = 0; i < subresources.size(); ++i)
		{
			const TextureSubresourceData& sub = subresources[i];
			if (sub.data == nullptr)
				continue;

			const uint32_t mip    = static_cast<uint32_t>(i % desc.mipLevels);
			const uint32_t slice  = static_cast<uint32_t>(i / desc.mipLevels);
			const uint32_t width  = std::max(1u, desc.width >> mip);
			const uint32_t height = std::max(1u, desc.height >> mip);

			const uint64_t rowBlocks = core::div_ceil<uint64_t>(width, format.blockEdgeTexels);
			const uint64_t colBlocks = core::div_ceil<uint64_t>(height, format.blockEdgeTexels);

			const uint64_t rowPitch =
				sub.rowPitch != 0 ? sub.rowPitch : rowBlocks * format.bytesPerBlock;
			const uint64_t byteSize = rowPitch * colBlocks;

			auto staging = NS::TransferPtr(
				m_Device->newBuffer(sub.data, byteSize, MTL::ResourceStorageModeShared));
			gassert(
				staging.get() != nullptr,
				"Metal texture upload staging buffer allocation failed");

			blit->copyFromBuffer(
				staging.get(),
				0,
				rowPitch,
				byteSize,
				MTL::Size(width, height, 1),
				dst,
				slice,
				mip,
				MTL::Origin(0, 0, 0));
		}
	}

	void
	CommandList::BeginEvent(std::string_view name) noexcept
	{
		m_CmdBuffer->pushDebugGroup(
			NS::String::string(std::string(name).c_str(), NS::UTF8StringEncoding));
	}

	void
	CommandList::EndEvent() noexcept
	{
		m_CmdBuffer->popDebugGroup();
	}

	void
	CommandList::BeginTiming(ITimestampHeap* heap, uint32_t startSlot, uint32_t endSlot) noexcept
	{
		gassert(m_Open, "BeginTiming on a closed command list");
		gassert(heap != nullptr, "BeginTiming needs a heap");
		gassert(m_TimingBuffer == nullptr, "BeginTiming while a timed span is open");
		gassert(
			startSlot < heap->GetCapacity() && endSlot < heap->GetCapacity(),
			"BeginTiming slot outside the heap");

		// A stage-boundary sample belongs to an encoder, so an encoder carried over from before the
		// span would have its start sampled where the previous work began.
		EndEncoder();

		m_TimingBuffer       = heap->As<TimestampHeap>()->GetMTLSampleBuffer();
		m_TimingStartSlot    = startSlot;
		m_TimingEndSlot      = endSlot;
		m_TimingStartSampled = false;
	}

	bool
	CommandList::EndTiming() noexcept
	{
		gassert(m_TimingBuffer != nullptr, "EndTiming without a timed span open");

		// The end sample is written where the last encoder ends, so it ends here rather than
		// running on into the next span's work.
		EndEncoder();

		const bool sampled   = m_TimingStartSampled;
		m_TimingBuffer       = nullptr;
		m_TimingStartSampled = false;
		return sampled;
	}

	void
	CommandList::SetMeshletState(const MeshletState& gfxState) noexcept
	{
		m_MeshletState = gfxState;
	}

	void
	CommandList::ApplyRenderState(MTL::RenderCommandEncoder* enc, const MeshletPipeline* pipeline)
		const noexcept
	{
		enc->setDepthStencilState(pipeline->GetMTLDepthStencilState());

		const RenderState& state = pipeline->GetDesc().renderState;
		enc->setCullMode(ConvertCullMode(state.rasterState.cullMode));
		enc->setFrontFacingWinding(
			state.rasterState.frontCounterClockwise ? MTL::WindingCounterClockwise :
													  MTL::WindingClockwise);
		enc->setTriangleFillMode(ConvertFillMode(state.rasterState.fillMode));
		enc->setDepthClipMode(
			state.rasterState.depthClipEnable ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp);
		enc->setDepthBias(
			static_cast<float>(state.rasterState.depthBias) *
				DepthBiasUnit(pipeline->GetDesc().dsvFormat),
			state.rasterState.slopeScaledDepthBias,
			state.rasterState.depthBiasClamp);

		if (state.depthStencilState.stencilEnable)
			enc->setStencilReferenceValue(state.depthStencilState.stencilRefValue);
	}

	CommandList::MeshletDraw
	CommandList::BindMeshletDraw() noexcept
	{
		gassert(m_Open, "A meshlet draw needs an open command list");
		gassert(m_MeshletState.kernel != nullptr, "A meshlet draw needs a meshlet state");

		auto* pipeline = m_MeshletState.kernel->pipeline->As<MeshletPipeline>();
		gassert(
			pipeline->GetMTLPipelineState() != nullptr,
			"A mesh-only pipeline has no pixel shader, so it cannot draw");
		auto* rm = m_ResourceManager->As<ResourceManager>();

		MTL::RenderCommandEncoder* enc = RenderEncoder(m_MeshletState.frameBuffer);
		enc->setRenderPipelineState(pipeline->GetMTLPipelineState());
		ApplyRenderState(enc, pipeline);

		// Each stage compiles as its own program, so a cbuffer's [[buffer(N)]] index is per-stage --
		// two cbuffers can share an N across stages. Bind only to the stages whose program declares
		// the cbuffer, at that stage's index. setBytes caps at 4KB, which cbuffers stay under.
		const bool hasObject     = m_MeshletState.kernel->pipeline->GetDesc().ampShader != nullptr;
		MTL::RenderStages stages = MTL::RenderStageMesh | MTL::RenderStageFragment;
		if (hasObject)
			stages |= MTL::RenderStageObject;

		const auto bindToStages = [&](std::string_view name, const void* bytes, size_t size) {
			if (const uint32_t* idx = pipeline->GetStageBinding(ShaderStage::kMesh, name))
				enc->setMeshBytes(bytes, size, *idx);
			if (const uint32_t* idx = pipeline->GetStageBinding(ShaderStage::kPixel, name))
				enc->setFragmentBytes(bytes, size, *idx);
			if (const uint32_t* idx = pipeline->GetStageBinding(ShaderStage::kAmplification, name);
			    idx != nullptr && hasObject)
				enc->setObjectBytes(bytes, size, *idx);
		};

		for (const auto& [name, uniforms] : m_MeshletState.kernel->uniforms)
		{
			const MappedUniform mapped =
				MapUniformHandlesToGpuAddresses(uniforms, pipeline->GetHandleOffsets(name), rm);

			for (MTL::Resource* resource : mapped.resident)
				enc->useResource(
					resource,
					MTL::ResourceUsageRead | MTL::ResourceUsageWrite,
					stages);

			bindToStages(name, mapped.bytes.data(), mapped.bytes.size());
		}

		// A material's texture id lives in GPU memory, so the encoder never sees the texture named.
		const std::span<MTL::Resource* const> textures = rm->GetLiveTextureResources();
		if (!textures.empty())
			enc->useResources(textures.data(), textures.size(), MTL::ResourceUsageRead, stages);

		if (auto it = m_MeshletState.kernel->uniforms.find("gDebug");
		    it != m_MeshletState.kernel->uniforms.end())
		{
			uint64_t     address = 0;
			MTL::Buffer* debug   = ActiveDebugBuffer(address);
			if (debug != nullptr)
				enc->useResource(debug, MTL::ResourceUsageRead | MTL::ResourceUsageWrite, stages);

			bindToStages("gDebug", &address, sizeof(address));
		}

		// Single viewport/scissor only; multi-viewport (setViewports + a viewport-index shader) lands
		// if a pass ever needs it.
		const auto& viewports = m_MeshletState.viewportState.viewports;
		if (!viewports.empty())
		{
			const Viewport& vp = viewports.front();
			enc->setViewport(
				MTL::Viewport{ vp.minX,
			                   vp.minY,
			                   vp.maxX - vp.minX,
			                   vp.maxY - vp.minY,
			                   vp.minZ,
			                   vp.maxZ });
		}
		const auto& scissors = m_MeshletState.viewportState.scissorRects;
		if (!scissors.empty())
		{
			const Rect& r = scissors.front();
			enc->setScissorRect(
				MTL::ScissorRect{ static_cast<NS::UInteger>(r.minX),
			                      static_cast<NS::UInteger>(r.minY),
			                      static_cast<NS::UInteger>(r.maxX - r.minX),
			                      static_cast<NS::UInteger>(r.maxY - r.minY) });
		}

		return { enc, pipeline };
	}

	void
	CommandList::DispatchMesh(uint32_t x, uint32_t y, uint32_t z) noexcept
	{
		const MeshletDraw draw = BindMeshletDraw();
		draw.encoder->drawMeshThreadgroups(
			MTL::Size(x, y, z),
			draw.pipeline->GetThreadsPerObjectThreadgroup(),
			draw.pipeline->GetThreadsPerMeshThreadgroup());
	}

	void
	CommandList::DispatchMeshIndirect(uint32_t argIdx) noexcept
	{
		gassert(
			!m_MeshletState.indirectArgs.IsNull(),
			"MeshletState.indirectArgs must be set for DispatchMeshIndirect");

		const MeshletDraw draw = BindMeshletDraw();
		auto*             rm   = m_ResourceManager->As<ResourceManager>();

		// MTLDispatchThreadgroupsIndirectArguments is the same three uint32s idl::DispatchArgs holds,
		// which is also D3D12's DISPATCH_MESH_ARGUMENTS, so one buffer feeds both backends unchanged.
		draw.encoder->drawMeshThreadgroups(
			rm->GetBuffer(m_MeshletState.indirectArgs).GetMTLResource(),
			static_cast<NS::UInteger>(argIdx) * sizeof(idl::DispatchArgs),
			draw.pipeline->GetThreadsPerObjectThreadgroup(),
			draw.pipeline->GetThreadsPerMeshThreadgroup());
	}

	MTL::Buffer*
	CommandList::ActiveDebugBuffer(uint64_t& outAddress) const noexcept
	{
		outAddress = 0;
#if defined(BERNINI_GPU_DEBUG)
		if (m_ActiveDebugBuffer.IsNull())
			return nullptr;

		auto*        rm     = m_ResourceManager->As<ResourceManager>();
		MTL::Buffer* buffer = rm->GetBuffer(m_ActiveDebugBuffer).GetMTLResource();
		outAddress          = buffer->gpuAddress();
		return buffer;
#else
		return nullptr;
#endif
	}

	void
	CommandList::SetComputeState(const ComputeState& computeState) noexcept
	{
		m_ComputeState = computeState;
	}

	void
	CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept
	{
		gassert(m_Open, "Dispatch on a closed command list");
		gassert(m_ComputeState.kernel != nullptr, "Dispatch without a compute state");

		auto* pipeline = m_ComputeState.kernel->pipeline->As<ComputePipeline>();
		auto* rm       = m_ResourceManager->As<ResourceManager>();

		auto* enc = GetComputeEncoder();
		enc->setComputePipelineState(pipeline->GetMTLPipelineState());

		for (const auto& [name, uniforms] : m_ComputeState.kernel->uniforms)
		{
			const UniformLayoutEntry entry = pipeline->GetUniformLayoutEntry(name);
			const MappedUniform      mapped =
				MapUniformHandlesToGpuAddresses(uniforms, pipeline->GetHandleOffsets(name), rm);

			for (MTL::Resource* resource : mapped.resident)
				enc->useResource(resource, MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

			enc->setBytes(mapped.bytes.data(), mapped.bytes.size(), entry.rootParamIndex);
		}

		// A texture id read out of GPU memory names a texture the encoder never sees, so nothing
		// above declares it resident. Without this a compute shader sampling through a handle it
		// loaded from a buffer reads whatever residency the rest of the frame happened to leave --
		// zeroes under GPU validation, which enforces it. The draw path does the same, and for the
		// same reason.
		const std::span<MTL::Resource* const> textures = rm->GetLiveTextureResources();
		if (!textures.empty())
			enc->useResources(textures.data(), textures.size(), MTL::ResourceUsageRead);

		if (auto it = m_ComputeState.kernel->uniforms.find("gDebug");
		    it != m_ComputeState.kernel->uniforms.end())
		{
			uint64_t     address = 0;
			MTL::Buffer* debug   = ActiveDebugBuffer(address);
			if (debug != nullptr)
				enc->useResource(debug, MTL::ResourceUsageRead | MTL::ResourceUsageWrite);
			enc->setBytes(
				&address,
				sizeof(address),
				pipeline->GetUniformLayoutEntry("gDebug").rootParamIndex);
		}

		enc->dispatchThreadgroups(MTL::Size(x, y, z), pipeline->GetThreadsPerThreadgroup());
	}
}
