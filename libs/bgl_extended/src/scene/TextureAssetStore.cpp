#include "scene/TextureAssetStore.h"
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/TextureDimension.h"
#include "types/vk_format.h"
#include "uniforms/DescriptorHandle.h"
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/IScene.h>
#include <bgl/TextureAssetHandle.h>
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
	TextureAssetStore::TextureAssetStore(core::SharedRef<IResourceManager> resourceManager) :
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
		const TextureHandle handle = Create(std::move(img), std::move(debugName));
		if (handle.IsNull())
		{
			return TextureAssetHandle{};
		}

		// Create made the view, so this lookup always hits.
		return TextureAssetHandle{ handle.slot, m_Srvs.at(handle.slot.index).srv.bindlessIndex };
	}

	TextureHandle
	TextureAssetStore::Create(assetlib::ImageData img, std::string debugName)
	{
		TextureDesc desc;
		desc.width     = img.width;
		desc.height    = img.height;
		desc.mipLevels = img.mipLevels;
		desc.arraySize = img.arraySize;
		desc.format    = FromVkFormat(img.vkFormat);
		desc.usage     = TextureUsageFlag::kSRV;
		desc.dimension =
			img.isCubemap ? TextureDimension::kTextureCube : TextureDimension::kTexture2D;
		desc.initialLayout = BarrierLayout::kCopyDest;
		desc.debugName     = std::move(debugName);

		const TextureHandle handle = m_ResourceManager->CreateTexture(desc);
		if (handle.IsNull())
		{
			return handle;
		}

		SrvDesc srvDesc;
		srvDesc.format    = desc.format;
		srvDesc.dimension = desc.dimension;
		srvDesc.mipLevels = desc.mipLevels;
		srvDesc.arraySize = desc.arraySize;
		srvDesc.debugName = desc.debugName;

		const SrvHandle srv = m_ResourceManager->CreateSrv(handle, srvDesc);
		if (srv.IsNull())
		{
			m_ResourceManager->DestroyTexture(handle, /*deferred*/ false);
			return TextureHandle{};
		}
		m_Srvs.emplace(handle.slot.index, Entry{ handle, srv });

		m_PendingUploads.push_back({ handle, std::move(img) });
		return handle;
	}

	TextureHandle
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
		const TextureHandle handle = TextureHandle::From(texture);
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
	TextureAssetStore::Flush(ICommandList* cmdList)
	{
		if (m_PendingUploads.empty())
		{
			return;
		}

		cmdList->BeginEvent("Scene Texture Uploads");

		std::vector<TextureHandle>      handles;
		std::vector<TextureBarrierDesc> barriers;
		handles.reserve(m_PendingUploads.size());
		barriers.reserve(m_PendingUploads.size());

		for (const PendingUpload& pending : m_PendingUploads)
		{
			std::vector<TextureSubresourceData> subresources;
			subresources.reserve(pending.image.subresources.size());
			for (const auto& s : pending.image.subresources)
			{
				subresources.push_back(
					{ pending.image.pixels.data() + s.offset, s.rowPitch, s.slicePitch });
			}

			cmdList->WriteTexture(pending.handle, subresources);

			// COPY_DEST -> SHADER_RESOURCE so the forward pass can sample it.
			TextureBarrierDesc barrier;
			barrier.syncBefore   = BarrierSyncFlag::kCopy;
			barrier.accessBefore = BarrierAccessFlag::kCopyDest;
			barrier.layoutBefore = BarrierLayout::kCopyDest;
			barrier.syncAfter    = BarrierSyncFlag::kPixelShader;
			barrier.accessAfter  = BarrierAccessFlag::kShaderResource;
			barrier.layoutAfter  = BarrierLayout::kShaderResource;

			handles.push_back(pending.handle);
			barriers.push_back(barrier);
		}

		cmdList->Barrier(handles, barriers);
		cmdList->EndEvent();
		m_PendingUploads.clear();
	}

	SrvHandle
	TextureAssetStore::GetSrv(core::slot_handle textureSlot) const noexcept
	{
		const auto it = m_Srvs.find(textureSlot.index);
		return it == m_Srvs.end() ? SrvHandle{} : it->second.srv;
	}

	DescriptorHandle
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
