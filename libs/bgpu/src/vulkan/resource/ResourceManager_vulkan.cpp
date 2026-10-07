#include "resource/ResourceManager_vulkan.h"
#include "cmd/CommandList_vulkan.h"
#include "convert_vulkan.h"
#include "native_device_vulkan.h"
#include "resource/BindlessTable_vulkan.h"
#include "resource/BoundedPool.h"
#include "resource/BufferMemory_vulkan.h"
#include "resource/Dsv_vulkan.h"
#include "resource/ImageMemory_vulkan.h"
#include "resource/ImageView_vulkan.h"
#include "resource/Rtv_vulkan.h"
#include "resource/Sampler_vulkan.h"
#include "resource/Srv_vulkan.h"
#include "resource/Texture_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <algorithm>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/FormatInfo.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/TextureDimension.h>
#include <bgpu/uniforms/DescriptorHandle.h>
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <core/containers/static_vector.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <exception>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	namespace
	{
		[[nodiscard]] VkImageCreateInfo
		ImageInfoOf(const TextureDesc& desc) noexcept
		{
			const bool line   = desc.dimension == TextureDimension::kTexture1D ||
			                    desc.dimension == TextureDimension::kTexture1DArray;
			const bool volume = desc.dimension == TextureDimension::kTexture3D;
			const bool cube   = desc.dimension == TextureDimension::kTextureCube ||
			                    desc.dimension == TextureDimension::kTextureCubeArray;
			core::ensure(!volume || desc.arraySize == 1, "A 3D texture cannot be an array");
			core::ensure(volume || desc.depth == 1, "Only a 3D texture has depth");

			auto info      = VkImageCreateInfo();
			info.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			info.imageType = line ? VK_IMAGE_TYPE_1D : volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
			info.format    = ConvertFormat(desc.format);
			info.extent    = VkExtent3D{ desc.width, line ? 1U : desc.height, desc.depth };
			info.mipLevels = desc.mipLevels;
			info.arrayLayers   = desc.arraySize;
			info.samples       = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
			info.tiling        = VK_IMAGE_TILING_OPTIMAL;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (cube)
				info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			// A render target view of a 3D texture names a range of its depth slices, which Vulkan
			// reaches as the layers of a 2D-array view.
			if (volume && desc.usage.any(TextureUsageFlag::kRenderTarget))
				info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
			// An SRV may read a colour texture in another format of its class, as one of D3D12's
			// typeless textures is read.
			if (FormatAspects(desc.format) == VK_IMAGE_ASPECT_COLOR_BIT)
				info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

			info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			             VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			if (desc.usage.any(TextureUsageFlag::kRenderTarget))
				info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			if (desc.usage.any(TextureUsageFlag::kDepthStencil))
				info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			return info;
		}

		// The layers a view of `dimension` covers, of a texture view desc's `arraySize`.
		[[nodiscard]] uint32_t
		ViewLayerCount(const TextureDimension dimension, const uint32_t arraySize) noexcept
		{
			switch (dimension)
			{
			case TextureDimension::kTexture1DArray:
			case TextureDimension::kTexture2DArray:
			case TextureDimension::kTexture2DMSArray:
			case TextureDimension::kTextureCubeArray:
				return arraySize;
			case TextureDimension::kTextureCube:
				return 6;
			default:
				return 1;
			}
		}
	}

	ResourceManager::ResourceManager(GpuContextRef context, const ResourceManagerDesc& desc) :
		m_Context(std::move(context)), m_Desc(desc),
		m_Table(GetVulkanHandles(*m_Context).device, desc.maxCbvSrvUavs),
		m_Buffers(desc.maxBuffers), m_BufferSrvs(desc.maxBufferSrvs),
		m_BufferUavs(desc.maxBufferUavs), m_ReadbackBuffers(desc.maxReadbackBuffers),
		m_Textures(desc.maxTextures), m_Srvs(desc.maxSrvs), m_Rtvs(desc.maxRtvs),
		m_Dsvs(desc.maxDsvs), m_Samplers(desc.maxSamplers)
	{
		core::ensure(desc.maxBuffers > 0, "maxBuffers must be greater than zero");
		core::ensure(
			desc.maxSamplers <= BindlessTable::c_SamplerCapacity,
			"maxSamplers is {}; the bindless table holds {}",
			desc.maxSamplers,
			BindlessTable::c_SamplerCapacity);
		// The +1 is the unbound sentinel the table burns at index 0 and never hands out.
		core::ensure(
			desc.maxCbvSrvUavs >=
				desc.maxBuffers + desc.maxSrvs + desc.maxBufferSrvs + desc.maxBufferUavs + 1,
			"maxCbvSrvUavs must cover every buffer, SRV and second view, and the unbound slot");
	}

	ResourceManager::~ResourceManager() noexcept { spdlog::trace("~ResourceManager"); }

	BufferHandle
	ResourceManager::AddBuffer(
		core::SharedRef<BufferMemory> memory,
		BufferDesc                    desc,
		const bool                    tracked) noexcept
	{
		const auto slot = TryAllocateBounded(m_Buffers);
		if (slot.is_null())
		{
			spdlog::error("Creating buffer '{}': buffer pool exhausted", desc.debugName);
			return BufferHandle{};
		}

		uint32_t descriptorIndex = 0xFFFFFFFF;
		try
		{
			descriptorIndex = m_Table.Allocate();
		}
		catch (const std::exception& e)
		{
			spdlog::error("Creating buffer '{}': {}", desc.debugName, e.what());
			m_Buffers.release_slot(slot.index);
			return BufferHandle{};
		}

		m_Table.WriteBuffer(descriptorIndex, memory->GetVkBuffer());
		m_Buffers[slot] = Buffer(std::move(memory), descriptorIndex, std::move(desc), tracked);
		return BufferHandle{ slot, descriptorIndex };
	}

	BufferHandle
	ResourceManager::CreateDeviceBuffer(BufferDesc desc) noexcept
	{
		// Out of device memory throws out of BufferMemory, and this is noexcept: caught here, or a
		// recoverable failure becomes a terminate.
		auto memory = core::SharedRef<BufferMemory>();
		try
		{
			memory = core::SharedRef<BufferMemory>::Make(
				m_Context,
				desc.byteSize,
				BufferMemoryKind::kDevice,
				desc.debugName);
		}
		catch (const std::exception& e)
		{
			spdlog::error(
				"Creating buffer '{}': allocating {} bytes of device memory failed: {}",
				desc.debugName,
				desc.byteSize,
				e.what());
			return BufferHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		return AddBuffer(std::move(memory), std::move(desc), true);
	}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		core::ensure(desc.stride > 0, "StructuredBuffer requires a valid structural stride");
		core::ensure(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		auto bufferDesc      = BufferDesc();
		bufferDesc.byteSize  = static_cast<uint64_t>(desc.stride) * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.allowsUav = desc.allowsUav;
		bufferDesc.debugName = desc.debugName;
		return CreateDeviceBuffer(std::move(bufferDesc));
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		core::ensure(desc.initialCount > 0, "ComputeBuffer requires a positive element count");
		core::ensure(desc.elementSize > 0, "ComputeBuffer requires a positive element size");

		auto structDesc         = StructBufferDesc();
		structDesc.stride       = desc.elementSize;
		structDesc.elementCount = desc.initialCount;
		structDesc.isUav        = true;
		structDesc.debugName    = desc.debugName;
		return CreateStructBuffer(structDesc);
	}

	BufferHandle
	ResourceManager::CreateRawBuffer(const RawViewDesc& desc) noexcept
	{
		core::ensure(desc.byteSize > 0, "A raw buffer requires a byte size");
		core::ensure(desc.byteSize % 4 == 0, "A raw view addresses whole 32-bit words");
		core::ensure(desc.byteSize <= c_MaxRawBufferBytes, "A raw view cannot address past 4 GiB");

		auto bufferDesc      = BufferDesc();
		bufferDesc.byteSize  = desc.byteSize;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.isRaw     = true;
		bufferDesc.debugName = desc.debugName;
		return CreateDeviceBuffer(std::move(bufferDesc));
	}

	core::slot_handle
	ResourceManager::AddView(
		core::slot_vector<uint32_t>& pool,
		const BufferHandle           buffer,
		const std::string_view       debugName,
		uint32_t&                    descriptorIndex) noexcept
	{
		const auto slot = TryAllocateBounded(pool);
		if (slot.is_null())
		{
			spdlog::error("Creating buffer view '{}': buffer view pool exhausted", debugName);
			return {};
		}

		try
		{
			descriptorIndex = m_Table.Allocate();
		}
		catch (const std::exception& e)
		{
			spdlog::error("Creating buffer view '{}': {}", debugName, e.what());
			pool.release_slot(slot.index);
			return {};
		}

		// A storage-buffer descriptor has no stride: the shader's declared type is the view.
		m_Table.WriteBuffer(descriptorIndex, m_Buffers[buffer.slot].GetVkBuffer());
		pool[slot.index] = descriptorIndex;
		return slot;
	}

	BufferSrvHandle
	ResourceManager::CreateBufferSrv(BufferHandle buffer, const BufferSrvDesc& desc) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferHandle(buffer), "CreateBufferSrv on an invalid buffer");
		core::ensure(desc.stride > 0, "A structured view requires a stride");
		core::ensure(
			m_Buffers[buffer.slot].GetDesc().byteSize % desc.stride == 0,
			"A structured view must divide the buffer it views");

		uint32_t   descriptorIndex = 0xFFFFFFFF;
		const auto slot            = AddView(m_BufferSrvs, buffer, desc.debugName, descriptorIndex);
		if (slot.is_null())
			return BufferSrvHandle{};
		return BufferSrvHandle{ slot, descriptorIndex };
	}

	void
	ResourceManager::DestroyBufferSrv(BufferSrvHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferSrvHandle(handle), "Cannot destroy invalid buffer view handle");

		const uint32_t descriptorIndex = m_BufferSrvs[handle.slot.index];
		if (deferred)
		{
			m_BufferSrvs.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kBufferSrv, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_BufferSrvs.release_slot(handle.slot.index);
			m_Table.Free(descriptorIndex);
		}
	}

	bool
	ResourceManager::ValidBufferSrvHandle(const BufferSrvHandle& handle) const noexcept
	{
		return m_BufferSrvs.valid(handle.slot);
	}

	BufferUavHandle
	ResourceManager::CreateBufferUav(BufferHandle buffer, const BufferUavDesc& desc) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferHandle(buffer), "CreateBufferUav on an invalid buffer");
		core::ensure(desc.stride > 0, "A structured view requires a stride");

		const BufferDesc& bufferDesc = m_Buffers[buffer.slot].GetDesc();
		core::ensure(
			bufferDesc.isUav || bufferDesc.allowsUav,
			"CreateBufferUav on a buffer created without allowsUav");
		core::ensure(!bufferDesc.isRaw, "CreateBufferUav on a raw buffer");
		core::ensure(
			bufferDesc.byteSize % desc.stride == 0,
			"A structured view must divide the buffer it views");

		uint32_t   descriptorIndex = 0xFFFFFFFF;
		const auto slot            = AddView(m_BufferUavs, buffer, desc.debugName, descriptorIndex);
		if (slot.is_null())
			return BufferUavHandle{};
		return BufferUavHandle{ slot, descriptorIndex };
	}

	void
	ResourceManager::DestroyBufferUav(BufferUavHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferUavHandle(handle), "Cannot destroy invalid buffer view handle");

		const uint32_t descriptorIndex = m_BufferUavs[handle.slot.index];
		if (deferred)
		{
			m_BufferUavs.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kBufferUav, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_BufferUavs.release_slot(handle.slot.index);
			m_Table.Free(descriptorIndex);
		}
	}

	bool
	ResourceManager::ValidBufferUavHandle(const BufferUavHandle& handle) const noexcept
	{
		return m_BufferUavs.valid(handle.slot);
	}

	NativeObject
	ResourceManager::GetNativeBuffer(BufferHandle handle, NativeObjectType type) const noexcept
	{
		if (type != NativeObjectType::kVkBuffer || !ValidBufferHandle(handle))
			return {};
		return NativeObject{ GetBuffer(handle).GetVkBuffer() };
	}

	BufferHandle
	ResourceManager::ImportNativeBuffer(const NativeBufferDesc& desc) noexcept
	{
		if (desc.type != NativeObjectType::kVkBuffer || desc.IsNull())
			return BufferHandle{};

		const uint64_t byteSize =
			static_cast<uint64_t>(desc.buffer.stride) * desc.buffer.elementCount;

		// Vulkan counts no references to a buffer, so only memory a manager made, which counts its
		// own, can be kept alive past its producer's release.
		auto memory = BufferMemory::Find(desc.object.As<VkBuffer_T>());
		if (memory == nullptr)
		{
			spdlog::error(
				"ImportNativeBuffer '{}': the buffer was not made by a bgpu resource manager",
				desc.buffer.debugName);
			return BufferHandle{};
		}
		core::ensure(
			GetVulkanHandles(*memory->GetContext()).device == GetVulkanHandles(*m_Context).device,
			"ImportNativeBuffer of another device's buffer");
		core::ensure(
			byteSize > 0 && byteSize <= memory->GetByteSize(),
			"ImportNativeBuffer views more bytes than the buffer holds");
		core::ensure(
			!desc.buffer.isUav && !desc.buffer.allowsUav,
			"An imported buffer is read-only");

		auto bufferDesc      = BufferDesc();
		bufferDesc.byteSize  = byteSize;
		bufferDesc.debugName = desc.buffer.debugName;

		const std::lock_guard lock(m_PoolMutex);
		return AddBuffer(std::move(memory), std::move(bufferDesc), false);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		if (m_Desc.maxTextures == 0)
		{
			spdlog::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		core::ensure(desc.format != Format::UNKNOWN, "A texture needs a format");
		// Refused here, before an image exists: kPresent ends the process.
		(void)ConvertImageLayout(desc.initialLayout);

		auto                info    = ImageInfoOf(desc);
		const VulkanSharing sharing = GetVulkanSharing(*m_Context);
		info.sharingMode            = sharing.mode;
		info.queueFamilyIndexCount  = static_cast<uint32_t>(sharing.families.size());
		info.pQueueFamilyIndices    = sharing.families.data();

		// Out of device memory throws out of ImageMemory, and this is noexcept.
		auto memory = core::SharedRef<ImageMemory>();
		try
		{
			memory = core::SharedRef<ImageMemory>::Make(m_Context, info, desc.debugName);
		}
		catch (const std::exception& e)
		{
			spdlog::error("CreateTexture '{}': {}", desc.debugName, e.what());
			return TextureHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		const auto            slot = TryAllocateBounded(m_Textures);
		if (slot.is_null())
		{
			spdlog::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}

		auto texture = Texture(std::move(memory), desc, true);
		if (desc.initialLayout != BarrierLayout::kUndefined)
			m_PendingLayouts.push_back({ texture.GetVkImage(), texture.GetAspects(), slot.index });
		m_Textures[slot] = std::move(texture);
		return TextureHandle{ slot };
	}

	NativeObject
	ResourceManager::GetNativeTexture(TextureHandle handle, NativeObjectType type) const noexcept
	{
		if (type != NativeObjectType::kVkImage || !ValidTextureHandle(handle))
			return {};
		return NativeObject{ TextureAt(handle).GetVkImage() };
	}

	TextureHandle
	ResourceManager::ImportNativeTexture(const NativeTextureDesc& desc) noexcept
	{
		if (desc.type != NativeObjectType::kVkImage || desc.IsNull())
			return TextureHandle{};
		core::ensure(desc.texture.format != Format::UNKNOWN, "An imported texture needs a format");

		auto* const image  = desc.object.As<VkImage_T>();
		auto        memory = ImageMemory::Find(image);
		core::ensure(
			memory == nullptr || GetVulkanHandles(*memory->GetContext()).device ==
									 GetVulkanHandles(*m_Context).device,
			"ImportNativeTexture of another device's image");

		const std::lock_guard lock(m_PoolMutex);
		const auto            slot = TryAllocateBounded(m_Textures);
		if (slot.is_null())
		{
			spdlog::error(
				"ImportNativeTexture '{}': texture pool exhausted",
				desc.texture.debugName);
			return TextureHandle{};
		}

		// Already in the layout its desc names, so no initial transition; and charged to its maker.
		m_Textures[slot] = memory != nullptr ? Texture(std::move(memory), desc.texture, false) :
		                                       Texture(image, desc.texture);
		return TextureHandle{ slot };
	}

	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc& desc) noexcept
	{
		if (m_Desc.maxSamplers == 0)
		{
			spdlog::error("CreateSampler: sampler pool exhausted");
			return SamplerHandle{};
		}

		auto sampler = Sampler();
		try
		{
			sampler = Sampler(GetVulkanHandles(*m_Context).device, desc);
		}
		catch (const std::exception& e)
		{
			spdlog::error("CreateSampler: {}", e.what());
			return SamplerHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		const auto            slot = TryAllocateBounded(m_Samplers);
		if (slot.is_null())
		{
			spdlog::error("CreateSampler: sampler pool exhausted");
			return SamplerHandle{};
		}

		// The sampler array is indexed by pool slot, as D3D12's sampler heap is.
		m_Table.WriteSampler(slot.index, sampler.GetVkSampler());
		m_Samplers[slot] = std::move(sampler);
		return SamplerHandle{ slot.index, slot.generation, slot.index };
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		core::ensure(desc.byteSize > 0, "Readback buffer requires a positive byte size");

		auto readback = ReadbackBuffer();
		try
		{
			readback = ReadbackBuffer(m_Context, desc);
		}
		catch (const std::exception& e)
		{
			spdlog::error("CreateReadbackBuffer '{}': {}", desc.debugName, e.what());
			return ReadbackBufferHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		const auto            slot = TryAllocateBounded(m_ReadbackBuffers);
		if (slot.is_null())
		{
			spdlog::error("CreateReadbackBuffer: readback pool exhausted");
			return ReadbackBufferHandle{};
		}
		m_ReadbackBuffers[slot.index] = std::move(readback);
		return ReadbackBufferHandle{ slot };
	}

	SrvHandle
	ResourceManager::CreateSrv(TextureHandle textureHandle, const SrvDesc& desc) noexcept
	{
		if (m_Desc.maxSrvs == 0)
		{
			spdlog::error("CreateSrv: SRV pool exhausted");
			return SrvHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidTextureHandle(textureHandle), "CreateSrv on an invalid texture");

		const auto slot = TryAllocateBounded(m_Srvs);
		if (slot.is_null())
		{
			spdlog::error("CreateSrv '{}': SRV pool exhausted", desc.debugName);
			return SrvHandle{};
		}

		uint32_t descriptorIndex = 0xFFFFFFFF;
		try
		{
			descriptorIndex = m_Table.Allocate();
		}
		catch (const std::exception& e)
		{
			spdlog::error("CreateSrv '{}': {}", desc.debugName, e.what());
			m_Srvs.release_slot(slot.index);
			return SrvHandle{};
		}

		const Texture& texture = TextureAt(textureHandle);

		auto range         = VkImageSubresourceRange();
		range.aspectMask   = ViewAspect(texture.GetAspects(), desc.format);
		range.baseMipLevel = 0;
		range.levelCount =
			desc.mipLevels == uint32_t(-1) ? VK_REMAINING_MIP_LEVELS : desc.mipLevels;
		range.baseArrayLayer = 0;
		range.layerCount     = ViewLayerCount(desc.dimension, desc.arraySize);

		// A depth view reads the image's own format through one aspect; a colour view may
		// reinterpret its class, since every colour image is made format-mutable.
		const VkFormat format =
			range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || desc.format == Format::UNKNOWN ?
				texture.GetVkFormat() :
				ConvertFormat(desc.format);

		auto view = ImageView(
			GetVulkanHandles(*m_Context).device,
			texture.GetVkImage(),
			ConvertImageViewType(desc.dimension),
			format,
			range,
			desc.debugName);
		m_Table.WriteImage(descriptorIndex, view.GetVkImageView());
		m_Srvs[slot] = Srv(std::move(view), textureHandle, descriptorIndex, desc);

		// As on D3D12, the table index is both what a cbuffer carries and what GPU memory carries.
		return SrvHandle{ slot.index,
			              slot.generation,
			              descriptorIndex,
			              DescriptorHandle(descriptorIndex) };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
		if (m_Desc.maxRtvs == 0)
		{
			spdlog::error("CreateRtv: RTV pool exhausted");
			return RtvHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidTextureHandle(textureHandle), "CreateRtv on an invalid texture");
		const Texture& texture = TextureAt(textureHandle);
		core::ensure(
			texture.GetDesc().usage.any(TextureUsageFlag::kRenderTarget),
			"CreateRtv on a texture made without kRenderTarget");

		const auto slot = TryAllocateBounded(m_Rtvs);
		if (slot.is_null())
		{
			spdlog::error("CreateRtv '{}': RTV pool exhausted", desc.debugName);
			return RtvHandle{};
		}

		auto range         = VkImageSubresourceRange();
		range.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = desc.mipSlice;
		range.levelCount   = 1;

		auto type = VK_IMAGE_VIEW_TYPE_2D;
		switch (desc.dimension)
		{
		case TextureDimension::kTexture1D:
			type             = VK_IMAGE_VIEW_TYPE_1D;
			range.layerCount = 1;
			break;
		case TextureDimension::kTexture2D:
		case TextureDimension::kTexture2DMS:
			range.layerCount = 1;
			break;
		case TextureDimension::kTexture1DArray:
			type                 = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			range.baseArrayLayer = desc.firstArraySlice;
			range.layerCount     = desc.arraySize;
			break;
		case TextureDimension::kTexture2DArray:
		case TextureDimension::kTexture2DMSArray:
			type                 = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			range.baseArrayLayer = desc.firstArraySlice;
			range.layerCount     = desc.arraySize;
			break;
		case TextureDimension::kTexture3D:
			type                 = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			range.baseArrayLayer = desc.firstWSlice;
			range.layerCount     = desc.wSize;
			break;
		case TextureDimension::kUnknown:
		case TextureDimension::kTextureCube:
		case TextureDimension::kTextureCubeArray:
			core::fatal(
				"An RTV of dimension {} is not one D3D12 has",
				static_cast<int>(desc.dimension));
		}

		const VkFormat format =
			desc.format == Format::UNKNOWN ? texture.GetVkFormat() : ConvertFormat(desc.format);
		auto view = ImageView(
			GetVulkanHandles(*m_Context).device,
			texture.GetVkImage(),
			type,
			format,
			range,
			desc.debugName);
		m_Rtvs[slot] = Rtv(std::move(view), range, textureHandle, desc);
		return RtvHandle{ slot.index, slot.generation };
	}

	DsvHandle
	ResourceManager::CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept
	{
		if (m_Desc.maxDsvs == 0)
		{
			spdlog::error("CreateDsv: DSV pool exhausted");
			return DsvHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidTextureHandle(textureHandle), "CreateDsv on an invalid texture");
		const Texture& texture = TextureAt(textureHandle);
		core::ensure(
			texture.GetDesc().usage.any(TextureUsageFlag::kDepthStencil),
			"CreateDsv on a texture made without kDepthStencil");

		const auto slot = TryAllocateBounded(m_Dsvs);
		if (slot.is_null())
		{
			spdlog::error("CreateDsv '{}': DSV pool exhausted", desc.debugName);
			return DsvHandle{};
		}

		// Every aspect the image has: a depth attachment is written as both.
		auto range         = VkImageSubresourceRange();
		range.aspectMask   = texture.GetAspects();
		range.baseMipLevel = desc.mipSlice;
		range.levelCount   = 1;

		auto type = VK_IMAGE_VIEW_TYPE_2D;
		switch (desc.dimension)
		{
		case TextureDimension::kTexture1D:
			type             = VK_IMAGE_VIEW_TYPE_1D;
			range.layerCount = 1;
			break;
		case TextureDimension::kTexture2D:
		case TextureDimension::kTexture2DMS:
			range.layerCount = 1;
			break;
		case TextureDimension::kTexture1DArray:
			type                 = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			range.baseArrayLayer = desc.firstArraySlice;
			range.layerCount     = desc.arraySize;
			break;
		case TextureDimension::kTexture2DArray:
		case TextureDimension::kTexture2DMSArray:
		case TextureDimension::kTextureCube:
		case TextureDimension::kTextureCubeArray:
			type                 = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			range.baseArrayLayer = desc.firstArraySlice;
			range.layerCount     = desc.arraySize;
			break;
		case TextureDimension::kUnknown:
		case TextureDimension::kTexture3D:
			core::fatal(
				"A DSV of dimension {} is not one D3D12 has",
				static_cast<int>(desc.dimension));
		}

		auto view = ImageView(
			GetVulkanHandles(*m_Context).device,
			texture.GetVkImage(),
			type,
			texture.GetVkFormat(),
			range,
			desc.debugName);
		m_Dsvs[slot] = Dsv(std::move(view), range, textureHandle, desc);
		return DsvHandle{ slot.index, slot.generation };
	}

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");

		const uint32_t descriptorIndex = m_Buffers[handle.slot].GetDescriptorIndex();
		if (deferred)
		{
			m_Buffers.retire_slot(handle.slot);
			RetireDeferred(PendingType::kBuffer, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_Buffers.release_slot(handle.slot);
			m_Table.Free(descriptorIndex);
		}
	}

	void
	ResourceManager::DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(
			ValidReadbackBufferHandle(handle),
			"Cannot destroy invalid readback buffer handle");

		if (deferred)
		{
			m_ReadbackBuffers.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kReadback, handle.slot.index);
		}
		else
		{
			m_ReadbackBuffers.release_slot(handle.slot.index);
		}
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");

		// A deferred texture keeps its pending transition: work recorded before the destroy may
		// still be submitted, and the image lives until its gate clears.
		if (deferred)
		{
			m_Textures.retire_slot(handle.slot);
			RetireDeferred(PendingType::kTexture, handle.slot.index);
		}
		else
		{
			ForgetInitialLayout(handle.slot.index);
			m_Textures.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::DestroySampler(SamplerHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidSamplerHandle(handle), "Cannot destroy invalid sampler handle");

		if (deferred)
		{
			m_Samplers.retire_slot(handle.idx);
			RetireDeferred(PendingType::kSampler, handle.idx);
		}
		else
		{
			m_Samplers.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::DestroySrv(SrvHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidSrvHandle(handle), "Cannot destroy invalid SRV handle");

		if (deferred)
		{
			m_Srvs.retire_slot(handle.idx);
			RetireDeferred(PendingType::kSrv, handle.idx, handle.bindlessIndex);
		}
		else
		{
			m_Srvs.release_slot(handle.idx);
			m_Table.Free(handle.bindlessIndex);
		}
	}

	void
	ResourceManager::DestroyRtv(RtvHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");

		if (deferred)
		{
			m_Rtvs.retire_slot(handle.idx);
			RetireDeferred(PendingType::kRtv, handle.idx);
		}
		else
		{
			m_Rtvs.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::DestroyDsv(DsvHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidDsvHandle(handle), "Cannot destroy invalid DSV handle");

		if (deferred)
		{
			m_Dsvs.retire_slot(handle.idx);
			RetireDeferred(PendingType::kDsv, handle.idx);
		}
		else
		{
			m_Dsvs.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::RegisterQueue(ICommandQueue* queue) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(queue != nullptr, "RegisterQueue requires a non-null queue");
		core::ensure(
			m_RegisteredQueues.size() < c_MaxRegisteredQueues,
			"More than c_MaxRegisteredQueues submission timelines registered");
		m_RegisteredQueues.push_back(queue);
	}

	void
	ResourceManager::UnregisterQueue(ICommandQueue* queue) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		for (uint32_t i = 0; i < m_RegisteredQueues.size(); ++i)
		{
			if (m_RegisteredQueues[i] == queue)
			{
				m_RegisteredQueues[i] = m_RegisteredQueues.back();
				m_RegisteredQueues.pop_back();
				break;
			}
		}

		// The queue has drained, so it gates nothing; dropping it from every gate keeps a freed
		// queue's address from aliasing a later one's.
		for (PendingDeletionBatch& batch : m_PendingBatches)
		{
			for (uint32_t i = 0; i < batch.gate.size(); ++i)
			{
				if (batch.gate[i].queue == queue)
				{
					batch.gate[i] = batch.gate.back();
					batch.gate.pop_back();
					break;
				}
			}
		}
	}

	DeletionGate
	ResourceManager::CaptureGate() const noexcept
	{
		auto gate = DeletionGate();
		for (ICommandQueue* queue : m_RegisteredQueues)
			gate.push_back({ queue, queue->GetNextFenceValue() });
		return gate;
	}

	void
	ResourceManager::RetireDeferred(
		const PendingType type,
		const uint32_t    slotIndex,
		const uint32_t    descriptorIndex) noexcept
	{
		const DeletionGate gate = CaptureGate();
		if (m_PendingBatches.empty() || !std::ranges::equal(m_PendingBatches.back().gate, gate))
			m_PendingBatches.push_back({ gate, {} });
		m_PendingBatches.back().deletions.push_back({ type, slotIndex, descriptorIndex });
	}

	void
	ResourceManager::CleanupExpiredResources() noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		auto                  completed = core::static_vector<QueueGate, c_MaxRegisteredQueues>();
		for (ICommandQueue* queue : m_RegisteredQueues)
			completed.push_back({ queue, queue->PollCurrentFenceValue() });

		const auto isCleared = [&](const QueueGate& entry) {
			for (const auto& [queue, value] : completed)
			{
				if (queue == entry.queue)
					return value >= entry.fenceValue;
			}
			// No longer registered: its owner flushed it before letting it go.
			return true;
		};

		const auto reclaim = [&](const PendingDeletion& pending) {
			switch (pending.type)
			{
			case PendingType::kBuffer:
				m_Buffers.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kBufferSrv:
				m_BufferSrvs.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kBufferUav:
				m_BufferUavs.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kReadback:
				m_ReadbackBuffers.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kTexture:
				ForgetInitialLayout(pending.slotIndex);
				m_Textures.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kSrv:
				m_Srvs.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kRtv:
				m_Rtvs.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kDsv:
				m_Dsvs.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kSampler:
				m_Samplers.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kInvalid:
				core::fatal("A pending deletion was recorded with no resource type");
			}
		};

		std::erase_if(m_PendingBatches, [&](const PendingDeletionBatch& batch) {
			if (!std::ranges::all_of(batch.gate, isCleared))
				return false;
			for (const PendingDeletion& pending : batch.deletions) reclaim(pending);
			return true;
		});
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const noexcept
	{
		core::ensure(ValidRtvHandle(handle), "GetRtv of an invalid RTV handle");
		return m_Rtvs[handle.idx];
	}

	const Dsv&
	ResourceManager::GetDsv(DsvHandle handle) const noexcept
	{
		core::ensure(ValidDsvHandle(handle), "GetDsv of an invalid DSV handle");
		return m_Dsvs[handle.idx];
	}

	TextureHandle
	ResourceManager::GetRtvTexture(RtvHandle handle) const noexcept
	{
		return GetRtv(handle).GetTextureHandle();
	}

	TextureHandle
	ResourceManager::GetDsvTexture(DsvHandle handle) const noexcept
	{
		return GetDsv(handle).GetTextureHandle();
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const noexcept
	{
		core::ensure(ValidBufferHandle(handle), "GetBuffer of an invalid buffer handle");
		return m_Buffers[handle.slot];
	}

	BufferDesc
	ResourceManager::GetBufferDesc(BufferHandle handle) const noexcept
	{
		return GetBuffer(handle).GetDesc();
	}

	const Texture&
	ResourceManager::TextureAt(const TextureHandle handle) const noexcept
	{
		return m_Textures[handle.slot];
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		core::ensure(ValidTextureHandle(handle), "GetTexture of an invalid texture handle");
		return TextureAt(handle);
	}

	TextureDesc
	ResourceManager::GetTextureDesc(TextureHandle handle) const noexcept
	{
		return GetTexture(handle).GetDesc();
	}

	const Sampler&
	ResourceManager::GetSampler(SamplerHandle handle) const noexcept
	{
		core::ensure(ValidSamplerHandle(handle), "GetSampler of an invalid sampler handle");
		return m_Samplers[handle.idx];
	}

	void
	ResourceManager::TakeInitialLayouts(std::vector<VkImageMemoryBarrier2>& barriers) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		for (const PendingLayout& pending : m_PendingLayouts)
		{
			auto barrier          = VkImageMemoryBarrier2();
			barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
			barrier.image                       = pending.image;
			barrier.subresourceRange.aspectMask = pending.aspects;
			barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
			barriers.push_back(barrier);
		}
		m_PendingLayouts.clear();
	}

	void
	ResourceManager::ForgetInitialLayout(const uint32_t textureSlot) noexcept
	{
		std::erase_if(m_PendingLayouts, [textureSlot](const PendingLayout& pending) {
			return pending.textureSlot == textureSlot;
		});
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		core::ensure(
			ValidReadbackBufferHandle(handle),
			"GetReadbackBuffer of an invalid readback handle");
		return m_ReadbackBuffers[handle.slot.index];
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle handle) const noexcept
	{
		return GetTexture(handle).GetReadbackLayout();
	}

	const void*
	ResourceManager::MapReadback(ReadbackBufferHandle handle) noexcept
	{
		return GetReadbackBuffer(handle).Map();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle handle) noexcept
	{
		// Mapped for its whole life; nothing to give back.
		core::ensure(
			ValidReadbackBufferHandle(handle),
			"UnmapReadback of an invalid readback handle");
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return m_Buffers.valid(handle.slot) && !m_Buffers[handle.slot].IsNull();
	}

	bool
	ResourceManager::ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept
	{
		return m_ReadbackBuffers.valid(handle.slot) &&
		       !m_ReadbackBuffers[handle.slot.index].IsNull();
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		return m_Textures.valid(handle.slot) && !m_Textures[handle.slot].IsNull();
	}

	bool
	ResourceManager::IsTextureCube(const TextureHandle& handle) const noexcept
	{
		if (!ValidTextureHandle(handle))
			return false;
		const TextureDimension dimension = TextureAt(handle).GetDesc().dimension;
		return dimension == TextureDimension::kTextureCube ||
		       dimension == TextureDimension::kTextureCubeArray;
	}

	bool
	ResourceManager::ValidSrvHandle(const SrvHandle& handle) const noexcept
	{
		return m_Srvs.valid(core::slot_handle{ handle.idx, handle.generation }) &&
		       !m_Srvs[handle.idx].IsNull();
	}

	bool
	ResourceManager::ValidSamplerHandle(const SamplerHandle& handle) const noexcept
	{
		return m_Samplers.valid(core::slot_handle{ handle.idx, handle.generation }) &&
		       !m_Samplers[handle.idx].IsNull();
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const noexcept
	{
		return m_Rtvs.valid(core::slot_handle{ handle.idx, handle.generation }) &&
		       !m_Rtvs[handle.idx].IsNull();
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle& handle) const noexcept
	{
		return m_Dsvs.valid(core::slot_handle{ handle.idx, handle.generation }) &&
		       !m_Dsvs[handle.idx].IsNull();
	}

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept
	{
		core::ensure(cmdList != nullptr, "ClearRtv needs a command list");
		const Rtv&     rtv     = GetRtv(handle);
		const Texture& texture = GetTexture(rtv.GetTextureHandle());

		// D3D12 takes floats and converts them to an integer target's own type.
		const FormatInfo info  = GetFormatInfo(texture.GetDesc().format);
		auto             value = VkClearColorValue();
		for (int i = 0; i < 4; ++i)
		{
			if (info.kind != FormatKind::kInteger)
				value.float32[i] = clearVal[i];
			else if (info.isSigned)
				value.int32[i] = static_cast<int32_t>(clearVal[i]);
			else
				value.uint32[i] = static_cast<uint32_t>(clearVal[i]);
		}
		cmdList->As<CommandList>()->ClearColor(texture.GetVkImage(), rtv.GetRange(), value);
	}

	void
	ResourceManager::ClearDsv(
		ICommandList* cmdList,
		DsvHandle     handle,
		float         depth,
		uint8_t       stencil) noexcept
	{
		core::ensure(cmdList != nullptr, "ClearDsv needs a command list");
		const Dsv&     dsv     = GetDsv(handle);
		const Texture& texture = GetTexture(dsv.GetTextureHandle());
		cmdList->As<CommandList>()->ClearDepthStencil(
			texture.GetVkImage(),
			dsv.GetRange(),
			VkClearDepthStencilValue{ depth, stencil });
	}
}
