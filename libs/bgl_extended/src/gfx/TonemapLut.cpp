#include "gfx/TonemapLut.h"
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/Format.h"
#include "types/TextureDimension.h"
#include <bgl/IGraphics.h>
#include <bgl_common/gassert.h>
#include <core/file/file.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr Format   c_Format        = Format::RGBA16_FLOAT;
		constexpr uint32_t c_BytesPerTexel = 8;
		constexpr size_t   c_HeaderBytes   = 16;
		constexpr uint32_t c_Version       = 1;

		struct Header
		{
			char     magic[4];
			uint32_t version;
			uint32_t size;
			uint32_t reserved;
		};
		static_assert(sizeof(Header) == c_HeaderBytes);
	}

	void
	TonemapLut::Init(ResourceManagerRef resourceManager, const std::filesystem::path& file)
	{
		m_ResourceManager = std::move(resourceManager);

		std::vector<std::byte> bytes;
		try
		{
			bytes = core::file::read_file_bytes(file);
		}
		catch (const std::exception& e)
		{
			throw GraphicsError("Tone map LUT '" + file.string() + "' cannot be read: " + e.what());
		}

		// The one LFS object device creation depends on: a checkout that never fetched has a
		// pointer file here, and "not a BLUT" would send someone to the wrong place.
		constexpr std::string_view c_LfsPointer = "version https://git-lfs.github.com/spec/v1";
		if (bytes.size() >= c_LfsPointer.size() &&
		    std::memcmp(bytes.data(), c_LfsPointer.data(), c_LfsPointer.size()) == 0)
			throw GraphicsError(
				"Tone map LUT '" + file.string() + "' is a Git LFS pointer: run `git lfs pull`");

		if (bytes.size() < c_HeaderBytes)
			throw GraphicsError("Tone map LUT '" + file.string() + "' is truncated");

		Header header;
		std::memcpy(&header, bytes.data(), c_HeaderBytes);
		if (std::memcmp(header.magic, "BLUT", 4) != 0 || header.version != c_Version)
			throw GraphicsError("Tone map LUT '" + file.string() + "' is not a BLUT version 1");

		const uint64_t texels = static_cast<uint64_t>(header.size) * header.size * header.size;
		if (header.size == 0 || bytes.size() != c_HeaderBytes + texels * c_BytesPerTexel)
			throw GraphicsError("Tone map LUT '" + file.string() + "' does not hold size^3 texels");

		m_Size = header.size;
		m_Pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(c_HeaderBytes), bytes.end());

		auto desc          = TextureDesc();
		desc.width         = m_Size * m_Size;
		desc.height        = m_Size;
		desc.format        = c_Format;
		desc.usage         = TextureUsageFlag::kSRV;
		desc.dimension     = TextureDimension::kTexture2D;
		desc.initialLayout = BarrierLayout::kCopyDest;
		desc.debugName     = "Tone map LUT";
		m_Texture          = m_ResourceManager->CreateTexture(desc);
		if (m_Texture.IsNull())
			throw GraphicsError("Tone map LUT texture could not be created");

		auto srvDesc      = SrvDesc();
		srvDesc.format    = c_Format;
		srvDesc.dimension = TextureDimension::kTexture2D;
		srvDesc.debugName = "Tone map LUT SRV";
		m_Srv             = m_ResourceManager->CreateSrv(m_Texture, srvDesc);
		if (m_Srv.IsNull())
			throw GraphicsError("Tone map LUT SRV could not be created");
	}

	void
	TonemapLut::Upload(ICommandList* cmdList)
	{
		gassert(cmdList != nullptr, "Command list must be initialized");
		gassert(!m_Pixels.empty(), "TonemapLut::Upload before Init, or twice");

		const uint64_t rowPitch = static_cast<uint64_t>(m_Size) * m_Size * c_BytesPerTexel;
		const TextureSubresourceData subresource{ m_Pixels.data(), rowPitch, rowPitch * m_Size };
		cmdList->WriteTexture(m_Texture, { &subresource, 1 });

		TextureBarrierDesc barrier;
		barrier.syncBefore   = BarrierSyncFlag::kCopy;
		barrier.accessBefore = BarrierAccessFlag::kCopyDest;
		barrier.layoutBefore = BarrierLayout::kCopyDest;
		barrier.syncAfter    = BarrierSyncFlag::kPixelShader | BarrierSyncFlag::kComputeShader;
		barrier.accessAfter  = BarrierAccessFlag::kShaderResource;
		barrier.layoutAfter  = BarrierLayout::kShaderResource;
		cmdList->Barrier(m_Texture, barrier);

		// WriteTexture copies the bytes into its staging before it returns; nothing reads them again.
		m_Pixels.clear();
		m_Pixels.shrink_to_fit();
	}

	void
	TonemapLut::Release() noexcept
	{
		if (!m_Srv.IsNull())
		{
			m_ResourceManager->DestroySrv(m_Srv, false);
			m_Srv = SrvHandle{};
		}
		if (!m_Texture.IsNull())
		{
			m_ResourceManager->DestroyTexture(m_Texture, false);
			m_Texture = TextureHandle{};
		}
		m_Pixels.clear();
	}
}
