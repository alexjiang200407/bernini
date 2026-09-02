#include "overlay/Overlay.h"
#include "cmd/CommandList.h"
#include "idl/OverlayVertex.h"
#include <bgl/IGraphics.h>

namespace bgl
{
	// The public vertex is what a client fills; the IDL struct is what the shader is generated
	// from. One is memcpy'd into a buffer the other reads, so they must agree to the byte.
	static_assert(sizeof(OverlayVertex) == sizeof(idl::OverlayVertex));
	static_assert(offsetof(OverlayVertex, position) == offsetof(idl::OverlayVertex, position));
	static_assert(offsetof(OverlayVertex, uv) == offsetof(idl::OverlayVertex, uv));
	static_assert(offsetof(OverlayVertex, color) == offsetof(idl::OverlayVertex, color));
	static_assert(offsetof(OverlayVertex, reserved) == offsetof(idl::OverlayVertex, reserved));

	namespace
	{
		uint32_t
		NextOverlayId() noexcept
		{
			static std::atomic<uint32_t> g_NextId{ 1 };
			return g_NextId.fetch_add(1, std::memory_order_relaxed);
		}
	}

	Overlay::Overlay(ResourceManagerRef resourceManager) :
		m_ResourceManager(std::move(resourceManager)), m_Id(NextOverlayId()),
		m_Images(m_ResourceManager)
	{
		gassert(m_ResourceManager != nullptr, "Overlay requires a valid ResourceManager");
	}

	Overlay::~Overlay() noexcept
	{
		for (uint32_t i = 0; i < m_Geometry.size(); ++i)
		{
			if (!m_Geometry.allocated(i))
			{
				continue;
			}

			const OverlayGeometry& geometry = m_Geometry[i];
			m_ResourceManager->DestroyBuffer(geometry.vertices);
			m_ResourceManager->DestroyBuffer(geometry.indices);
		}
	}

	OverlayGeometryHandle
	Overlay::CreateGeometry(
		std::span<const OverlayVertex> vertices,
		std::span<const uint32_t>      indices)
	{
		if (vertices.empty() || indices.empty())
		{
			throw GraphicsError("Overlay geometry needs at least one vertex and one index");
		}

		if (indices.size() % 3 != 0)
		{
			throw GraphicsError(
				"Overlay geometry is a triangle list; its index count must be a "
				"multiple of three");
		}

		for (const uint32_t index : indices)
		{
			if (index >= vertices.size())
			{
				throw GraphicsError(
					std::format(
						"Overlay geometry index {} is out of range for {} vertices",
						index,
						vertices.size()));
			}
		}

		auto vertexDesc = StructBufferDesc();
		vertexDesc.SetElement<OverlayVertex>().SetElementCount(
			static_cast<uint32_t>(vertices.size()));
		vertexDesc.debugName = "Overlay Vertices";

		const BufferHandle vertexBuffer = m_ResourceManager->CreateStructBuffer(vertexDesc);
		if (vertexBuffer.IsNull())
		{
			throw GraphicsError("The device could not allocate an overlay vertex buffer");
		}

		auto indexDesc = StructBufferDesc();
		indexDesc.SetElement<uint32_t>().SetElementCount(static_cast<uint32_t>(indices.size()));
		indexDesc.debugName = "Overlay Indices";

		const BufferHandle indexBuffer = m_ResourceManager->CreateStructBuffer(indexDesc);
		if (indexBuffer.IsNull())
		{
			m_ResourceManager->DestroyBuffer(vertexBuffer, /*deferred*/ false);
			throw GraphicsError("The device could not allocate an overlay index buffer");
		}

		const core::slot_handle slot = m_Geometry.allocate_and_emplace(
			OverlayGeometry{ vertexBuffer,
		                     indexBuffer,
		                     static_cast<uint32_t>(indices.size() / 3) });

		m_PendingGeometry.push_back(
			{ slot,
		      std::vector<OverlayVertex>(vertices.begin(), vertices.end()),
		      std::vector<uint32_t>(indices.begin(), indices.end()) });

		return OverlayGeometryHandle{ slot, m_Id };
	}

	void
	Overlay::ReleaseGeometry(OverlayGeometryHandle geometry)
	{
		if (!ValidGeometry(geometry))
		{
			throw GraphicsError(
				"OverlayGeometryHandle passed to ReleaseGeometry is null, expired, or not this "
				"overlay's");
		}

		// A release can arrive before any frame drew it, in which case the upload goes with it.
		std::erase_if(m_PendingGeometry, [&](const PendingGeometry& pending) {
			return pending.slot == geometry.slot;
		});

		const OverlayGeometry& record = m_Geometry[geometry.slot];
		m_ResourceManager->DestroyBuffer(record.vertices);
		m_ResourceManager->DestroyBuffer(record.indices);

		m_Geometry.release_slot(geometry.slot);
	}

	OverlayTextureHandle
	Overlay::CreateTexture(assetlib::ImageData img, std::string debugName)
	{
		if (img.width == 0 || img.height == 0 || img.subresources.empty() || img.pixels.empty())
		{
			throw GraphicsError("Overlay texture needs decoded pixels");
		}

		const TextureAssetHandle image = m_Images.Add(
			std::move(img),
			debugName.empty() ? std::string("Overlay Texture") : std::move(debugName));

		if (image.textureSlot.is_null())
		{
			throw GraphicsError("The device could not allocate an overlay texture");
		}

		const core::slot_handle slot =
			m_Textures.allocate_and_emplace(OverlayTexture{ image, nullptr });

		return OverlayTextureHandle{ slot, m_Id };
	}

	OverlayTextureHandle
	Overlay::CreateTexture(const RenderTargetRef& target)
	{
		if (target == nullptr)
		{
			throw GraphicsError("Overlay texture needs a render target");
		}

		auto* source = target->As<RenderTargetBase>();
		gassert(source != nullptr, "An IRenderTarget this graphics did not create");

		if (!source->IsHeadless())
		{
			throw GraphicsError(
				"Overlay texture cannot wrap a windowed target: its swapchain image is the surface "
				"a frame presents to");
		}

		const core::slot_handle slot = m_Textures.allocate_and_emplace(
			OverlayTexture{ TextureAssetHandle{}, core::SharedRef<RenderTargetBase>(source) });

		return OverlayTextureHandle{ slot, m_Id };
	}

	void
	Overlay::ReleaseTexture(OverlayTextureHandle texture)
	{
		if (!ValidTexture(texture))
		{
			throw GraphicsError(
				"OverlayTextureHandle passed to ReleaseTexture is null, expired, or not this "
				"overlay's");
		}

		const OverlayTexture& record = m_Textures[texture.slot];
		if (record.target == nullptr)
		{
			m_Images.Delete(record.image);
		}

		m_Textures.release_slot(texture.slot);
	}

	bool
	Overlay::ValidGeometry(OverlayGeometryHandle geometry) const noexcept
	{
		return geometry.IsValid() && geometry.overlay == m_Id && m_Geometry.valid(geometry.slot);
	}

	const OverlayGeometry&
	Overlay::GetGeometry(OverlayGeometryHandle geometry) const
	{
		gassert(ValidGeometry(geometry), "GetGeometry needs a live handle");
		return m_Geometry[geometry.slot];
	}

	bool
	Overlay::ValidTexture(OverlayTextureHandle texture) const noexcept
	{
		return texture.IsValid() && texture.overlay == m_Id && m_Textures.valid(texture.slot);
	}

	SrvHandle
	Overlay::GetTextureSrv(OverlayTextureHandle texture) const noexcept
	{
		const SrvHandle white =
			m_Images.GetSrv(m_Images.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite));

		if (!texture.IsValid())
		{
			return white;
		}

		gassert(ValidTexture(texture), "GetTextureSrv needs a live handle");
		if (!ValidTexture(texture))
		{
			return white;
		}

		const OverlayTexture& record = m_Textures[texture.slot];
		if (record.target == nullptr)
		{
			return m_Images.GetSrv(record.image.textureSlot);
		}

		// A target that has not presented since it was created or resized has no frame to show.
		return record.target->HasPresented() ?
		           record.target->GetBackbufferSrv(record.target->GetLastPresentedIndex()) :
		           white;
	}

	RenderTargetBase*
	Overlay::GetTextureTarget(OverlayTextureHandle texture) const noexcept
	{
		gassert(
			!texture.IsValid() || ValidTexture(texture),
			"GetTextureTarget needs a live handle");
		return ValidTexture(texture) ? m_Textures[texture.slot].target.Get() : nullptr;
	}

	void
	Overlay::Flush(ICommandList* cmdList)
	{
		gassert(cmdList != nullptr, "Flush requires a valid ICommandList");

		if (!m_PendingGeometry.empty())
		{
			cmdList->BeginEvent("Overlay Geometry Uploads");

			std::vector<BufferHandle>      buffers;
			std::vector<BufferBarrierDesc> barriers;
			buffers.reserve(m_PendingGeometry.size() * 2);

			for (const PendingGeometry& pending : m_PendingGeometry)
			{
				const OverlayGeometry& geometry = m_Geometry[pending.slot];

				cmdList->WriteBuffer(
					geometry.vertices,
					pending.vertices.data(),
					pending.vertices.size() * sizeof(OverlayVertex));
				cmdList->WriteBuffer(
					geometry.indices,
					pending.indices.data(),
					pending.indices.size() * sizeof(uint32_t));

				buffers.push_back(geometry.vertices);
				buffers.push_back(geometry.indices);
			}

			// Not frame-graph resources -- one overlay may hold hundreds -- so the copy-to-read
			// barrier is issued here, as the texture store issues its own.
			barriers.assign(
				buffers.size(),
				BufferBarrierDesc()
					.AddSyncBefore(BarrierSyncFlag::kCopy)
					.AddAccessBefore(BarrierAccessFlag::kCopyDest)
					.AddSyncAfter(BarrierSyncFlag::kVertexShader)
					.AddSyncAfter(BarrierSyncFlag::kPixelShader)
					.AddAccessAfter(BarrierAccessFlag::kShaderResource));

			cmdList->Barrier(buffers, barriers);
			cmdList->EndEvent();
			m_PendingGeometry.clear();
		}

		m_Images.Flush(cmdList);
	}
}
