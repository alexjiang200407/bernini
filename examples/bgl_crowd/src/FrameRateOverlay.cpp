#include "FrameRateOverlay.h"
#include <RmlUi/Core.h>
#include <core/err/util.h>
#include <format>

namespace crowd_example
{
	namespace
	{
		// Copied beside the executable by copy_assets, as every example's bundled data is.
		constexpr auto c_BundledData = "assets/Data";
		constexpr auto c_Font        = "Authored/Fonts/Lato-Regular.ttf";
		constexpr auto c_Document    = R"(<rml><head><style>
			body { width: 100%; height: 100%; font-family: Lato; }
			#fps { position: absolute; left: 12px; top: 10px; padding: 4px 8px;
			       font-size: 18px; color: #ffffff; background-color: #00000099; }
		</style></head><body><div id="fps">-- fps</div></body></rml>)";

		// Long enough to read, short enough to follow a change.
		constexpr double c_RefreshSeconds = 0.5;
	}

	FrameRateOverlay::FrameRateOverlay(bgl::IGraphics& graphics, uint32_t width, uint32_t height) :
		m_Store(c_BundledData), m_Renderer(graphics, m_Store),
		m_Runtime(m_Store, m_Renderer.Interface())
	{
		m_Runtime.LoadFontFace(c_Font);
		m_Context = m_Runtime.CreateContext("frame_rate", width, height);

		Rml::ElementDocument* document =
			m_Context->Get().LoadDocumentFromMemory(c_Document, "Authored/UI/frame_rate.rml");
		if (document == nullptr)
			core::throw_runtime_error("the frame rate document could not be loaded");
		document->Show();
		m_Text = document->GetElementById("fps");
	}

	void
	FrameRateOverlay::Tick(double seconds)
	{
		m_Gathered += seconds;
		++m_Frames;
		if (m_Gathered < c_RefreshSeconds)
			return;

		m_Text->SetInnerRML(
			std::format(
				"{:.0f} fps &nbsp; {:.2f} ms",
				static_cast<double>(m_Frames) / m_Gathered,
				1000.0 * m_Gathered / static_cast<double>(m_Frames)));
		m_Gathered = 0.0;
		m_Frames   = 0;
	}

	void
	FrameRateOverlay::Render(bgl::IGraphics& graphics)
	{
		m_Context->Get().Update();
		m_Renderer.Render(graphics, *m_Context);
	}
}
