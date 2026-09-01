#pragma once
#include "resource/ResourceManager.h"
#include <bgl/TextureAssetHandle.h>

namespace bgl
{
	class ICommandList;

	/**
	 * Every texture asset a scene owns: the resource, the shader resource view that reaches it, and
	 * the decoded pixels not yet uploaded.
	 *
	 * The pixels are held rather than written on arrival because an upload must be ordered against
	 * the frames that sample it, and only the owner knows which context draws them -- Flush is where
	 * that context's command list arrives.
	 */
	class TextureAssetStore
	{
	public:
		// 1x1 textures a material channel falls back to when nothing is routed to it: white for
		// base color / ORM, a flat tangent-space normal (0.5,0.5,1).
		enum class DefaultTexture : uint32_t
		{
			kWhite,
			kFlatNormal,
			kCount
		};

		explicit TextureAssetStore(core::SharedRef<IResourceManager> resourceManager);

		// Releases every texture and view still held -- the defaults, and whatever the owner did not
		// Delete -- deferred behind the frames that could still sample them.
		~TextureAssetStore() noexcept;

		TextureAssetStore(const TextureAssetStore&) noexcept = delete;
		TextureAssetStore(TextureAssetStore&&) noexcept      = delete;

		TextureAssetStore&
		operator=(const TextureAssetStore&) noexcept = delete;

		TextureAssetStore&
		operator=(TextureAssetStore&&) noexcept = delete;

		/**
		 * Creates the texture and its view, and queues `img` for upload at the next Flush.
		 *
		 * @return a null handle, with nothing queued, when the texture or descriptor pool is
		 *         exhausted.
		 */
		[[nodiscard]] TextureAssetHandle
		Add(assetlib::ImageData img, std::string debugName);

		/**
		 * Releases the texture and its view, and drops the upload if one is still queued.
		 *
		 * @throws SceneError if the handle has expired or never named a live texture.
		 */
		void
		Delete(TextureAssetHandle texture);

		/**
		 * Uploads everything queued since the last call and barriers it to shader-resource.
		 *
		 * These bindless textures are not frame-graph resources, so the barrier is issued directly.
		 */
		void
		Flush(ICommandList* cmdList);

		// The view created for the texture in `textureSlot`, or a null handle if this store created
		// none.
		[[nodiscard]] SrvHandle
		GetSrv(core::slot_handle textureSlot) const noexcept;

		/**
		 * The descriptor a GPU struct must carry to reach the texture in `textureSlot`, or a null
		 * one when that texture has no view. Null is not an error: a material may name a channel no
		 * texture was ever routed to.
		 */
		[[nodiscard]] DescriptorHandle
		GetDescriptor(core::slot_handle textureSlot) const noexcept;

		[[nodiscard]] core::slot_handle
		GetDefaultSlot(DefaultTexture kind) const noexcept;

	private:
		[[nodiscard]] TextureHandle
		Create(assetlib::ImageData img, std::string debugName);

		// A 1x1 RGBA8 texture through the same deferred-upload path as any loaded image.
		[[nodiscard]] TextureHandle
		CreateSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

		// Decoded pixels awaiting upload. Held by value: the caller's ImageData is consumed on Add,
		// so the bytes must outlive it.
		struct PendingUpload
		{
			TextureHandle       handle;
			assetlib::ImageData image;

			PendingUpload(TextureHandle texture, assetlib::ImageData data) noexcept :
				handle(texture), image(std::move(data))
			{}

			PendingUpload(PendingUpload&&) noexcept = default;
			PendingUpload(const PendingUpload&)     = delete;

			PendingUpload&
			operator=(PendingUpload&&) noexcept = default;

			PendingUpload&
			operator=(const PendingUpload&) = delete;
		};

		struct Entry
		{
			TextureHandle texture;
			SrvHandle     srv;
		};

		core::SharedRef<IResourceManager> m_ResourceManager;

		// Keyed by the texture's slot index. A texture carries no descriptor of its own, and
		// destroying one does not cascade to its views, so whoever created both releases both.
		std::unordered_map<uint32_t, Entry> m_Srvs;

		std::vector<PendingUpload> m_PendingUploads;

		std::array<TextureHandle, static_cast<size_t>(DefaultTexture::kCount)> m_Defaults;
	};
}
