#pragma once

#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string_view>
#include <vector>

namespace bgl
{
	// Under the staged shader tree, which is what a device resolves "./shaders/src" against.
	constexpr std::string_view c_TonemapLutFile = "shaders/src/luts/agx_base_srgb.bin";

	/**
	 * The display curve's LUT: Blender's AgX formation as `scripts/gen_agx_lut.py` wrote it, a
	 * strip of `size` slices uploaded once and sampled by the post pass through `TonemapLut` in
	 * lib/screen. See docs/passes.md.
	 *
	 * Two steps because the bytes need a command list: Init reads the file and creates the texture,
	 * Upload records the write on whichever list the caller has open and drops the bytes.
	 */
	class TonemapLut
	{
	public:
		TonemapLut() = default;
		~TonemapLut() noexcept { logger::trace("~TonemapLut"); }
		TonemapLut(const TonemapLut&) noexcept = delete;
		TonemapLut(TonemapLut&&) noexcept      = delete;
		TonemapLut&
		operator=(const TonemapLut&) noexcept = delete;
		TonemapLut&
		operator=(TonemapLut&&) noexcept = delete;

		/**
		 * Reads the strip at `file` and creates the texture it fills.
		 *
		 * @throws GraphicsError if the file cannot be read, is not a `BLUT` version 1, or the
		 *         texture cannot be created.
		 */
		void
		Init(ResourceManagerRef resourceManager, const std::filesystem::path& file);

		/** Records the upload and the barrier that makes it sampleable. @pre Init succeeded. */
		void
		Upload(ICommandList* cmdList);

		[[nodiscard]] SrvHandle
		GetSrv() const noexcept
		{
			return m_Srv;
		}

		void
		Release() noexcept;

	private:
		ResourceManagerRef     m_ResourceManager;
		TextureHandle          m_Texture;
		SrvHandle              m_Srv;
		std::vector<std::byte> m_Pixels;
		uint32_t               m_Size = 0;
	};
}
