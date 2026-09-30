#include "scene/TextureAssetStore.h"
#include "types/vk_format.h"
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/IScene.h>
#include <bgl/types/TextureAssetHandle.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/TextureDimension.h>
#include <bgpu/uniforms/DescriptorHandle.h>
#include <core/containers/fixed_buffer.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bgl
{
	TextureAssetStore::TextureAssetStore(core::SharedRef<bgpu::IResourceManager> resourceManager) :
		m_ResourceManager(std::move(resourceManager))
	{
		m_Defaults[static_cast<size_t>(DefaultTexture::kWhite)] = CreateSolid(255, 255, 255, 255);
		m_Defaults[static_cast<size_t>(DefaultTexture::kFlatNormal)] =
			CreateSolid(128, 128, 255, 255);
	}

	TextureAssetStore::~TextureAssetStore() noexcept
	{
		for (const auto& [index, entry] : m_Srvs)
		{
			m_ResourceManager->DestroySrv(entry.srv);
			m_ResourceManager->DestroyTexture(entry.texture);
		}
	}

	TextureAssetHandle
	TextureAssetStore::Add(assetlib::ImageData img, std::string debugName)
	{
		const bgpu::TextureHandle handle = Create(std::move(img), std::move(debugName));
		if (handle.IsNull())
		{
			return TextureAssetHandle{};
		}

		// Create made the view, so this lookup always hits.
		return TextureAssetHandle{ handle.slot, m_Srvs.at(handle.slot.index).srv.bindlessIndex };
	}

	bgpu::TextureHandle
	TextureAssetStore::Create(assetlib::ImageData img, std::string debugName)
	{
		bgpu::TextureDesc desc;
		desc.width         = img.width;
		desc.height        = img.height;
		desc.mipLevels     = img.mipLevels;
		desc.arraySize     = img.arraySize;
		desc.format        = FromVkFormat(img.vkFormat);
		desc.usage         = bgpu::TextureUsageFlag::kSRV;
		desc.dimension     = img.isCubemap ? bgpu::TextureDimension::kTextureCube :
		                                     bgpu::TextureDimension::kTexture2D;
		desc.initialLayout = bgpu::BarrierLayout::kCopyDest;
		desc.debugName     = std::move(debugName);

		const bgpu::TextureHandle handle = m_ResourceManager->CreateTexture(desc);
		if (handle.IsNull())
		{
			return handle;
		}

		bgpu::SrvDesc srvDesc;
		srvDesc.format    = desc.format;
		srvDesc.dimension = desc.dimension;
		srvDesc.mipLevels = desc.mipLevels;
		srvDesc.arraySize = desc.arraySize;
		srvDesc.debugName = desc.debugName;

		const bgpu::SrvHandle srv = m_ResourceManager->CreateSrv(handle, srvDesc);
		if (srv.IsNull())
		{
			m_ResourceManager->DestroyTexture(handle, /*deferred*/ false);
			return bgpu::TextureHandle{};
		}
		m_Srvs.emplace(handle.slot.index, Entry{ handle, srv });

		m_PendingUploads.push_back({ handle, std::move(img) });
		return handle;
	}

	bgpu::TextureHandle
	TextureAssetStore::CreateSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		auto img     = assetlib::ImageData();
		img.width    = 1;
		img.height   = 1;
		img.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
		img.pixels   = core::fixed_buffer<std::byte>(4);

		const uint8_t pixel[4] = { r, g, b, a };
		std::memcpy(img.pixels.data(), pixel, sizeof(pixel));
		img.subresources.push_back({ 0, 4, 4 });

		return Create(std::move(img), "Solid Texture");
	}

	void
	TextureAssetStore::Delete(TextureAssetHandle texture)
	{
		// Destroying retires the slot at once, so a texture already deleted fails this check even
		// while the GPU is still finishing with it. There is nothing to remember here.
		const bgpu::TextureHandle handle = TextureHandleOf(texture);
		if (handle.IsNull() || !m_ResourceManager->ValidTextureHandle(handle))
		{
			throw SceneError(
				"TextureAssetHandle passed to DeleteTextureAsset has expired or is invalid");
		}

		// A delete can arrive before Flush ever ran -- a caller may release a texture without a
		// frame in between (e.g. a render that failed before drawing). The slot retires now, so the
		// queued write must go with it or the flush writes a stale handle.
		std::erase_if(m_PendingUploads, [&](const PendingUpload& pending) {
			return pending.handle == handle;
		});

		// Frames already submitted may still sample this texture, so only the *release* is deferred:
		// the resource manager recycles the bindless slot no earlier than the last frame that could
		// read it. The view is not cascaded to by DestroyTexture, so it goes on the same gate.
		if (const auto it = m_Srvs.find(handle.slot.index); it != m_Srvs.end())
		{
			m_ResourceManager->DestroySrv(it->second.srv);
			m_Srvs.erase(it);
		}

		m_ResourceManager->DestroyTexture(handle);
	}

	void
	TextureAssetStore::Flush(bgpu::ICommandList* cmdList)
	{
		if (m_PendingUploads.empty())
		{
			return;
		}

		cmdList->BeginEvent("Scene Texture Uploads");

		std::vector<bgpu::TextureHandle>      handles;
		std::vector<bgpu::TextureBarrierDesc> barriers;
		handles.reserve(m_PendingUploads.size());
		barriers.reserve(m_PendingUploads.size());

		for (const PendingUpload& pending : m_PendingUploads)
		{
			std::vector<bgpu::TextureSubresourceData> subresources;
			subresources.reserve(pending.image.subresources.size());
			for (const auto& s : pending.image.subresources)
			{
				subresources.push_back(
					{ pending.image.pixels.data() + s.offset, s.rowPitch, s.slicePitch });
			}

			cmdList->WriteTexture(pending.handle, subresources);

			// COPY_DEST -> SHADER_RESOURCE so the forward pass can sample it.
			bgpu::TextureBarrierDesc barrier;
			barrier.syncBefore   = bgpu::BarrierSyncFlag::kCopy;
			barrier.accessBefore = bgpu::BarrierAccessFlag::kCopyDest;
			barrier.layoutBefore = bgpu::BarrierLayout::kCopyDest;
			barrier.syncAfter    = bgpu::BarrierSyncFlag::kPixelShader;
			barrier.accessAfter  = bgpu::BarrierAccessFlag::kShaderResource;
			barrier.layoutAfter  = bgpu::BarrierLayout::kShaderResource;

			handles.push_back(pending.handle);
			barriers.push_back(barrier);
		}

		cmdList->Barrier(handles, barriers);
		cmdList->EndEvent();
		m_PendingUploads.clear();
	}

	bgpu::SrvHandle
	TextureAssetStore::GetSrv(core::slot_handle textureSlot) const noexcept
	{
		const auto it = m_Srvs.find(textureSlot.index);
		return it == m_Srvs.end() ? bgpu::SrvHandle{} : it->second.srv;
	}

	bgpu::DescriptorHandle
	TextureAssetStore::GetDescriptor(core::slot_handle textureSlot) const noexcept
	{
		return GetSrv(textureSlot).descriptor;
	}

	core::slot_handle
	TextureAssetStore::GetDefaultSlot(DefaultTexture kind) const noexcept
	{
		return m_Defaults[static_cast<size_t>(kind)].slot;
	}
}
