#pragma once
#include <assetlib/AssetStore.h>
#include <bgl/IGraphics.h>
#include <gamelib/ui/UiRuntime.h>

namespace Rml
{
	class RenderInterface;
}

namespace game
{
	/**
	 * What RmlUi draws through: its geometry becomes `bgl::OverlayDraw`s on a `bgl::IOverlay`, and
	 * its textures become the overlay's -- decoded from the mount, or another render target's
	 * output.
	 *
	 * Held by the client and handed to `UiRuntime`, which is why it is a class of its own rather
	 * than something the runtime owns: it needs an `IGraphics` the runtime knows nothing about,
	 * and a headless test swaps it for a stub.
	 *
	 * The RmlUi side is hidden behind an implementation the header does not name, so a client that
	 * only draws a UI never compiles an RmlUi header (ADR-7).
	 */
	class UiRenderer final
	{
	public:
		/**
		 * @param graphics the device the overlay is created on; must outlive this renderer.
		 * @param store    where a document's images are read from; must outlive this renderer.
		 * @throws bgl::GraphicsError if the overlay cannot be created.
		 */
		UiRenderer(bgl::IGraphics& graphics, const assetlib::AssetStore& store);
		~UiRenderer() noexcept;

		UiRenderer(const UiRenderer&)     = delete;
		UiRenderer(UiRenderer&&) noexcept = delete;

		UiRenderer&
		operator=(const UiRenderer&) = delete;

		UiRenderer&
		operator=(UiRenderer&&) noexcept = delete;

		/** What `UiRuntime` is constructed with. Valid for this renderer's lifetime. */
		[[nodiscard]] Rml::RenderInterface&
		Interface() noexcept;

		/**
		 * Draws `context` into the frame `graphics` currently has open: RmlUi walks the document
		 * and hands over its geometry, and all of it is submitted as one overlay job, after the
		 * scene and after post-processing.
		 *
		 * @pre a frame is open -- called between `BeginFrame` and `EndFrame`.
		 * @throws bgl::GraphicsError if it is not.
		 */
		void
		Render(bgl::IGraphics& graphics, UiContext& context);

		/**
		 * Names a render target a document can show: `<img src="target://preview"/>` draws what
		 * that target last presented, which is how a live 3D render sits inside the UI (ADR-14).
		 *
		 * The target is retained until it is replaced or this renderer is destroyed. Registering
		 * over a name a document already resolved does not move that document's texture; register
		 * before the document loads.
		 *
		 * @throws std::runtime_error if `name` is empty or `target` is null.
		 */
		void
		RegisterTarget(std::string name, bgl::RenderTargetRef target);

	private:
		class Impl;

		std::unique_ptr<Impl> m_Impl;
	};
}
