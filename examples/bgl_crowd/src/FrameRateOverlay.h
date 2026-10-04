#pragma once
#include <assetlib/AssetStore.h>
#include <bgl/IGraphics.h>
#include <cstdint>
#include <gamelib/ui/UiRenderer.h>
#include <gamelib/ui/UiRuntime.h>

namespace Rml
{
	class Element;
}

namespace crowd_example
{
	/**
	 * The frame rate in the window's corner: frames a second and milliseconds a frame, averaged
	 * over half a second so it can be read. Drawn by RmlUi through the renderer's overlay, with the
	 * engine's bundled font, after the frame's scene and before its end.
	 */
	class FrameRateOverlay
	{
	public:
		/** @throws std::runtime_error if the bundled font or the document cannot be loaded. */
		FrameRateOverlay(bgl::IGraphics& graphics, uint32_t width, uint32_t height);

		FrameRateOverlay(const FrameRateOverlay&) = delete;
		FrameRateOverlay&
		operator=(const FrameRateOverlay&) = delete;

		/** Counts a frame of `seconds`, and refreshes the text once half a second has gathered. */
		void
		Tick(double seconds);

		/** Draws the text. @pre between `graphics`'s BeginFrame and EndFrame. */
		void
		Render(bgl::IGraphics& graphics);

	private:
		assetlib::AssetStore m_Store;
		game::UiRenderer     m_Renderer;
		game::UiRuntime      m_Runtime;
		game::UiContextPtr   m_Context;
		Rml::Element*        m_Text = nullptr;

		double   m_Gathered = 0.0;
		uint32_t m_Frames   = 0;
	};
}
