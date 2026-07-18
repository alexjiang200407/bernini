#pragma once
#include <bgl/IRenderTarget.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * A windowed Metal render output. Wraps the CAMetalLayer the caller's view owns; each frame
	 * acquires a drawable from it. `RenderTargetDesc::wnd` is the CAMetalLayer for a Metal target
	 * (the demo window hands one over via SDL_Metal_GetLayer).
	 */
	class RenderTarget final : public core::RefCounter<IRenderTarget>
	{
	public:
		RenderTarget(const RenderTargetDesc& desc, id<MTLDevice> device);
		~RenderTarget() noexcept override;

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

		CAMetalLayer*
		Layer() const noexcept
		{
			return m_Layer;
		}

		void
		Resize(uint32_t width, uint32_t height) noexcept;

	private:
		CAMetalLayer* m_Layer  = nil;
		uint32_t      m_Width  = 0;
		uint32_t      m_Height = 0;
	};
}
