#pragma once
#include "gfx/RenderTargetBase.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "resource/Srv.h"
#include "scene/TextureAssetStore.h"
#include <assetlib_structs/ImageData.h>
#include <bgl/IOverlay.h>
#include <bgl/IRenderTarget.h>
#include <bgl/TextureAssetHandle.h>
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bgl
{
	class ICommandList;

	// A compiled geometry: a vertex buffer and an index buffer, both bindless.
	struct OverlayGeometry
	{
		BufferHandle vertices;
		BufferHandle indices;
		uint32_t     triangleCount = 0;
	};

	// What an OverlayTextureHandle names: an image in the store, or a target whose latest output is
	// read. Exactly one is set.
	struct OverlayTexture
	{
		TextureAssetHandle                image;
		core::SharedRef<RenderTargetBase> target;
	};

	/**
	 * The IOverlay behind IGraphics::CreateOverlay. Its images ride the same store a scene's do.
	 *
	 * Like a scene's textures, the bytes are held until Flush, which the overlay pass calls on the
	 * command list of the frame that draws them -- the upload must be ordered against the frames
	 * that read it, and only that list is.
	 */
	class Overlay final : public core::RefCounter<IOverlay>
	{
	public:
		explicit Overlay(ResourceManagerRef resourceManager);
		~Overlay() noexcept override;

		Overlay(const Overlay&) noexcept = delete;
		Overlay(Overlay&&) noexcept      = delete;

		Overlay&
		operator=(const Overlay&) noexcept = delete;

		Overlay&
		operator=(Overlay&&) noexcept = delete;

		OverlayGeometryHandle
		CreateGeometry(std::span<const OverlayVertex> vertices, std::span<const uint32_t> indices)
			override;

		void
		ReleaseGeometry(OverlayGeometryHandle geometry) override;

		OverlayTextureHandle
		CreateTexture(assetlib::ImageData img, std::string debugName) override;

		OverlayTextureHandle
		CreateTexture(const RenderTargetRef& target) override;

		void
		ReleaseTexture(OverlayTextureHandle texture) override;

		[[nodiscard]] bool
		ValidGeometry(OverlayGeometryHandle geometry) const noexcept;

		// @pre ValidGeometry(geometry)
		[[nodiscard]] const OverlayGeometry&
		GetGeometry(OverlayGeometryHandle geometry) const;

		[[nodiscard]] bool
		ValidTexture(OverlayTextureHandle texture) const noexcept;

		// A null handle resolves to the store's opaque white, so an untextured draw samples 1. A
		// target-backed one resolves to the slot that target last presented, so the answer changes
		// as the target draws -- and to white until it has presented at all.
		[[nodiscard]] SrvHandle
		GetTextureSrv(OverlayTextureHandle texture) const noexcept;

		// The target behind a texture, or null for an image or a null handle.
		[[nodiscard]] RenderTargetBase*
		GetTextureTarget(OverlayTextureHandle texture) const noexcept;

		/** Uploads every geometry and texture created since the last call, on `cmdList`. */
		void
		Flush(ICommandList* cmdList);

	private:
		struct PendingGeometry
		{
			core::slot_handle          slot;
			std::vector<OverlayVertex> vertices;
			std::vector<uint32_t>      indices;
		};

		ResourceManagerRef m_ResourceManager;

		// Process-unique and never zero, so a default-constructed handle matches no overlay.
		uint32_t m_Id = 0;

		core::slot_vector<OverlayGeometry> m_Geometry;
		std::vector<PendingGeometry>       m_PendingGeometry;

		core::slot_vector<OverlayTexture> m_Textures;
		TextureAssetStore                 m_Images;
	};
}
