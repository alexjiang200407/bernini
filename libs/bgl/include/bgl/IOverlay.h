#pragma once
#include <assetlib_structs/ImageData.h>
#include <bgl/api.h>
#include <bgl/glm.h>
#include <core/containers/slot_handle.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * One corner of an overlay triangle. `position` is in output pixels, origin top-left, y down.
	 * `uv` addresses the draw's texture, (0,0) top-left. `color` is RGBA8 with red in the low
	 * byte, sRGB-encoded and premultiplied by its alpha -- the form a UI library hands over.
	 *
	 * 24 bytes, laid out so a renderer reading it with natural 8-byte vector alignment sees the
	 * same bytes as one reading it packed; `reserved` is the padding that makes the two agree.
	 * The renderer asserts this against the struct its shaders are generated from.
	 */
	struct OverlayVertex
	{
		glm::vec2 position{ 0.0f };
		glm::vec2 uv{ 0.0f };
		uint32_t  color    = 0xFFFFFFFFu;
		uint32_t  reserved = 0;
	};

	static_assert(sizeof(OverlayVertex) == 24);

	// A handle names its overlay as well as its slot: every overlay numbers slots from zero, so the
	// slot alone could not tell one overlay's geometry from another's.
	struct OverlayGeometryHandle
	{
		core::slot_handle slot;
		uint32_t          overlay = 0;

		[[nodiscard]] bool
		IsValid() const noexcept
		{
			return !slot.is_null();
		}
	};

	struct OverlayTextureHandle
	{
		core::slot_handle slot;
		uint32_t          overlay = 0;

		[[nodiscard]] bool
		IsValid() const noexcept
		{
			return !slot.is_null();
		}
	};

	/** A pixel rectangle in output space, origin top-left. */
	struct OverlayRect
	{
		int x      = 0;
		int y      = 0;
		int width  = 0;
		int height = 0;
	};

	/**
	 * One draw of one compiled geometry. `transform` applies to the translated pixel positions
	 * before they are projected; `scissor` discards every pixel outside it.
	 */
	struct OverlayDraw
	{
		OverlayGeometryHandle geometry;

		// A null handle draws the geometry with its vertex colors alone.
		OverlayTextureHandle texture;

		glm::vec2                  translation{ 0.0f };
		std::optional<glm::mat4>   transform;
		std::optional<OverlayRect> scissor;
	};

	/**
	 * Compiled 2D geometry and the textures it samples, drawn over a frame after post-processing
	 * with premultiplied blending and no depth. General 2D output, of which a UI runtime is one
	 * client: nothing here knows what a document is.
	 *
	 * Textures are straight-alpha and sampled through the format their `ImageData` declares, so an
	 * sRGB format decodes on sample; vertex colors are decoded by the renderer.
	 *
	 * A geometry or texture drawn by an IGraphics::DrawOverlay must not be released before that
	 * frame's EndFrame. Releasing it afterwards is safe at any time: the GPU resources go once the
	 * frames that could still read them have retired.
	 */
	class BGL_API IOverlay : public core::Ref
	{
	public:
		IOverlay(IOverlay&&) noexcept      = delete;
		IOverlay(const IOverlay&) noexcept = delete;

		IOverlay&
		operator=(IOverlay&&) noexcept = delete;

		IOverlay&
		operator=(const IOverlay&) noexcept = delete;

		/**
		 * Compiles a triangle list. The bytes are copied; nothing reaches the GPU until a frame
		 * that draws this overlay is submitted.
		 *
		 * @throws GraphicsError if either span is empty, `indices` is not a multiple of three, an
		 *         index is out of range, or the device cannot allocate.
		 */
		virtual OverlayGeometryHandle
		CreateGeometry(
			std::span<const OverlayVertex> vertices,
			std::span<const uint32_t>      indices) = 0;

		/**
		 * @throws GraphicsError if the handle is null, expired, or was never minted by this overlay.
		 */
		virtual void
		ReleaseGeometry(OverlayGeometryHandle geometry) = 0;

		/**
		 * @throws GraphicsError if the image has no pixels or the device cannot allocate.
		 */
		virtual OverlayTextureHandle
		CreateTexture(assetlib::ImageData img, std::string debugName = "") = 0;

		/**
		 * @throws GraphicsError if the handle is null, expired, or was never minted by this overlay.
		 */
		virtual void
		ReleaseTexture(OverlayTextureHandle texture) = 0;

	protected:
		IOverlay() noexcept = default;
	};

	using OverlayRef = core::SharedRef<IOverlay>;

	/**
	 * What one IGraphics::DrawOverlay submits: draws in the order they are to land. The draws are
	 * copied by the call, so the span need not outlive it.
	 */
	struct OverlayJob
	{
		OverlayRef                   overlay = nullptr;
		std::span<const OverlayDraw> draws;
	};
}

template class BGL_API core::SharedRef<bgl::IOverlay>;
