#pragma once
#include "metal_cpp.h"

#include <bgl/IRenderTarget.h>
#include <core/err/util.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * A windowed Metal render output. Wraps the CA::MetalLayer the caller's view owns (SDL hands one
	 * over via SDL_Metal_GetLayer); `RenderTargetDesc::wnd` is that layer for a Metal target. Each
	 * frame acquires a drawable from it. The layer is not owned here -- the view owns it.
	 */
	class RenderTarget final : public core::RefCounter<IRenderTarget>
	{
	public:
		RenderTarget(const RenderTargetDesc& desc, MTL::Device* device);
		~RenderTarget() noexcept override = default;

		uint32_t
		GetWidth() const noexcept override
		{
			return m_Width;
		}

		uint32_t
		GetHeight() const noexcept override
		{
			return m_Height;
		}

		CA::MetalLayer*
		Layer() const noexcept
		{
			return m_Layer;
		}

		void
		Resize(uint32_t width, uint32_t height) noexcept;

	private:
		CA::MetalLayer* m_Layer  = nullptr;
		uint32_t        m_Width  = 0;
		uint32_t        m_Height = 0;
	};
}
