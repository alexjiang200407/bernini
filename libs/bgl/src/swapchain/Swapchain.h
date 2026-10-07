#pragma once
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/types/Format.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace bgl
{
	/**
	 * A window's presentable images, as the backend's API makes them: all a RenderTarget needs of a
	 * backend to present. The target imports the images as textures and draws into them; the
	 * swapchain decides which one a frame draws into and shows it.
	 *
	 * Called from the thread that drives the target, never concurrently.
	 */
	class Swapchain
	{
	public:
		Swapchain() noexcept          = default;
		virtual ~Swapchain() noexcept = default;

		Swapchain(const Swapchain&) = delete;
		Swapchain(Swapchain&&)      = delete;
		Swapchain&
		operator=(const Swapchain&) = delete;
		Swapchain&
		operator=(Swapchain&&) = delete;

		/**
		 * Every image, as `ImportNativeTexture` adopts it; element i is image i. The descs name
		 * `kPresent` as the initial layout. Valid until the next `Resize`.
		 */
		[[nodiscard]] virtual std::vector<bgpu::NativeTextureDesc>
		GetImages() const = 0;

		/** The format a render target view of an image writes: the sRGB view of the images. */
		[[nodiscard]] virtual bgpu::Format
		GetViewFormat() const noexcept = 0;

		/** The image the next frame draws into. */
		[[nodiscard]] virtual uint32_t
		GetCurrentImage() const noexcept = 0;

		/**
		 * Whether an image never presented since the swapchain was made is undefined rather than
		 * in `kPresent`: the first frame to draw it must then discard rather than transition.
		 */
		[[nodiscard]] virtual bool
		StartsUndefined() const noexcept = 0;

		/** Whether a presented image may still be read, as a capture of the last frame does. */
		[[nodiscard]] virtual bool
		CanReadPresented() const noexcept = 0;

		/**
		 * Shows the current image once the queue reaches `frameFence`, the fence of the submission
		 * that drew it, then takes the next image -- blocking, as a full queue of presents does,
		 * until one is free.
		 *
		 * @return true when the window no longer matched the images and the swapchain remade them,
		 *         at the window's size, after idling the queue. The textures imported from the
		 *         old images then name images that are gone: they are released, every image is
		 *         imported again, and the target takes on their size if it changed.
		 */
		[[nodiscard]] virtual bool
		Present(uint64_t frameFence) noexcept = 0;

		/**
		 * Remakes the images at the size asked for, or at the nearest the window allows: what was
		 * made is what GetImages reports.
		 *
		 * @pre the queue is idle, and no image is imported as a texture.
		 */
		virtual void
		Resize(uint32_t width, uint32_t height) = 0;
	};

	/**
	 * The backend's swapchain over `desc.wnd` (an HWND on Windows), presenting on `queue`.
	 *
	 * @throws GraphicsError when the window cannot be presented to.
	 */
	[[nodiscard]] std::unique_ptr<Swapchain>
	CreateBackendSwapchain(
		const RenderTargetDesc&      desc,
		const bgpu::DeviceRef&       device,
		const bgpu::CommandQueueRef& queue,
		bool                         enableDebug);
}
