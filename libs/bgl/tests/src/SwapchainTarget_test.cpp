// The render target every swapchain backend shares, driven through a fake swapchain: which image a
// ring slot draws into, what a present is told to wait for, the layout a fresh image is imported in,
// and what a resize starts over. What the fake cannot show -- that a real swapchain puts the image on
// screen -- is each backend's to prove with a window.
//
// Metal's target is its own and has no swapchain, so the case excludes itself there.
#if !defined(RENDERER_BACKEND_METAL)
#	include "gfx/frame_constants.h"
#	include "swapchain/RenderTarget.h"
#	include "swapchain/ISwapchain.h"
#	include "gfx/GraphicsBase.h"
#	include "util/TestGraphics.h"
#	include <bgl/IGraphics.h>
#	include <bgl/IRenderTarget.h>
#	include <bgpu/cmd/CommandQueue.h>
#	include <bgpu/device/Device.h>
#	include <bgpu/resource/NativeTextureDesc.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/resource/Texture.h>
#	include <bgpu/types/Barrier.h>
#	include <bgpu/types/Format.h>
#	include <bgpu/types/NativeObject.h>
#	include <bgpu/types/QueueType.h>
#	include <catch2/catch_test_macros.hpp>
#	include <core/ref/SharedRef.h>
#	include <cstdint>
#	include <format>
#	include <memory>
#	include <utility>
#	include <vector>

namespace
{
	// What a frame-graph backend would native-name it on each backend.
	constexpr auto c_NativeType =
#	if defined(RENDERER_BACKEND_VULKAN)
		bgpu::NativeObjectType::kVkImage;
#	else
		bgpu::NativeObjectType::kD3D12Resource;
#	endif

	// More images than the ring has slots, as a Vulkan driver may make, handed out in turn.
	constexpr uint32_t c_ImageCount = 3;

	struct FakeLog
	{
		std::vector<uint64_t> presentedFences;
		uint32_t              resizes = 0;
		// The next present finds the window changed and remakes the images, as Vulkan's does: at
		// the size given, or at the one they had when it is zero.
		bool     remakeAtNextPresent = false;
		uint32_t remakeWidth         = 0;
		uint32_t remakeHeight        = 0;
		// A resize makes the images at this size whatever it is asked, as a window Vulkan presents
		// to does; zero is no window, and the size asked for.
		uint32_t windowWidth  = 0;
		uint32_t windowHeight = 0;
	};

	class FakeSwapchain final : public bgl::ISwapchain
	{
	public:
		FakeSwapchain(
			bgpu::IResourceManager& rm,
			FakeLog&                log,
			const uint32_t          width,
			const uint32_t          height) : m_Rm(rm), m_Log(log)
		{
			MakeImages(width, height);
		}

		~FakeSwapchain() noexcept override { DestroyImages(); }

		FakeSwapchain(const FakeSwapchain&) = delete;
		FakeSwapchain(FakeSwapchain&&)      = delete;
		FakeSwapchain&
		operator=(const FakeSwapchain&) = delete;
		FakeSwapchain&
		operator=(FakeSwapchain&&) = delete;

		[[nodiscard]] std::vector<bgpu::NativeTextureDesc>
		GetImages() const override
		{
			auto images = std::vector<bgpu::NativeTextureDesc>();
			for (const bgpu::TextureHandle image : m_Images)
			{
				auto desc          = m_Rm.GetTextureDesc(image);
				desc.initialLayout = bgpu::BarrierLayout::kPresent;
				images.push_back(
					bgpu::NativeTextureDesc()
						.SetObject(c_NativeType, m_Rm.GetNativeTexture(image, c_NativeType))
						.SetTexture(desc));
			}
			return images;
		}

		[[nodiscard]] bgpu::Format
		GetViewFormat() const noexcept override
		{
			return bgpu::Format::SBGRA8_UNORM;
		}

		[[nodiscard]] uint32_t
		GetCurrentImage() const noexcept override
		{
			return m_Current;
		}

		[[nodiscard]] bool
		StartsUndefined() const noexcept override
		{
			return true;
		}

		[[nodiscard]] bool
		CanReadPresented() const noexcept override
		{
			return false;
		}

		[[nodiscard]] bool
		Present(const uint64_t frameFence) noexcept override
		{
			m_Log.presentedFences.push_back(frameFence);
			if (m_Log.remakeAtNextPresent)
			{
				m_Log.remakeAtNextPresent    = false;
				const bgpu::TextureDesc desc = m_Rm.GetTextureDesc(m_Images[0]);
				DestroyImages();
				MakeImages(
					m_Log.remakeWidth != 0 ? m_Log.remakeWidth : desc.width,
					m_Log.remakeHeight != 0 ? m_Log.remakeHeight : desc.height);
				m_Current = 0;
				return true;
			}
			m_Current = (m_Current + 1) % c_ImageCount;
			return false;
		}

		void
		Resize(const uint32_t width, const uint32_t height) override
		{
			++m_Log.resizes;
			DestroyImages();
			MakeImages(
				m_Log.windowWidth != 0 ? m_Log.windowWidth : width,
				m_Log.windowHeight != 0 ? m_Log.windowHeight : height);
			m_Current = 0;
		}

		[[nodiscard]] void*
		NativeImage(const uint32_t index) const noexcept
		{
			return m_Rm.GetNativeTexture(m_Images[index], c_NativeType).pointer;
		}

	private:
		void
		MakeImages(const uint32_t width, const uint32_t height)
		{
			for (uint32_t i = 0; i < c_ImageCount; ++i)
			{
				auto desc      = bgpu::TextureDesc();
				desc.width     = width;
				desc.height    = height;
				desc.format    = bgpu::Format::BGRA8_UNORM;
				desc.usage     = bgpu::TextureUsageFlag::kRenderTarget;
				desc.debugName = std::format("fake swapchain image {}", i);
				m_Images.push_back(m_Rm.CreateTexture(desc));
			}
		}

		void
		DestroyImages() noexcept
		{
			for (const bgpu::TextureHandle image : m_Images) m_Rm.DestroyTexture(image, false);
			m_Images.clear();
		}

		bgpu::IResourceManager&          m_Rm;
		FakeLog&                         m_Log;
		std::vector<bgpu::TextureHandle> m_Images;
		uint32_t                         m_Current = 0;
	};

	// A queue and manager of the case's own, on the device of the suite's one context.
	struct Owner
	{
		Owner() :
			graphics(bgl::test::CreateGraphics(Setup())),
			device(graphics->As<bgl::GraphicsBase>()->GetDevice()),
			queue(device->CreateCommandQueue(bgpu::QueueType::kGraphics)),
			rm(device->CreateResourceManager(bgpu::ResourceManagerDesc()))
		{
			rm->RegisterQueue(queue.Get());
		}

		~Owner()
		{
			queue->Flush();
			rm->UnregisterQueue(queue.Get());
		}

		Owner(const Owner&) = delete;
		Owner(Owner&&)      = delete;
		Owner&
		operator=(const Owner&) = delete;
		Owner&
		operator=(Owner&&) = delete;

		static bgl::test::GraphicsSetup
		Setup()
		{
			auto setup                        = bgl::test::GraphicsSetup();
			setup.gpuContext.enableDebugLayer = true;
			return setup;
		}

		bgl::GraphicsRef         graphics;
		bgpu::DeviceRef          device;
		bgpu::CommandQueueRef    queue;
		bgpu::ResourceManagerRef rm;
	};

	void*
	BackbufferImage(
		const bgl::RenderTarget& target,
		bgpu::IResourceManager&  rm,
		const uint32_t           slot)
	{
		return rm.GetNativeTexture(target.GetBackbufferTexture(slot), c_NativeType).pointer;
	}
}

TEST_CASE(
	"A swapchain target maps each ring slot to the image its swapchain handed out",
	"[render][swapchain]")
{
	auto owner = Owner();
	auto log   = FakeLog();

	auto desc   = bgl::RenderTargetDesc();
	desc.width  = 8;
	desc.height = 8;

	auto  swapchain = std::make_unique<FakeSwapchain>(*owner.rm, log, 8, 8);
	auto* fake      = swapchain.get();
	auto  target    = core::SharedRef<bgl::RenderTarget>::Make(
		desc,
		std::move(swapchain),
		owner.device,
		owner.queue,
		owner.rm);

	CHECK_FALSE(target->IsCapturable());

	// Five frames walk the three images round while the ring's two slots alternate, and each
	// present is told the fence of the frame recorded in its slot.
	for (uint32_t frame = 0; frame < 5; ++frame)
	{
		const uint32_t slot = target->GetFrameIndex();
		CHECK(slot == frame % bgl::c_SwapchainImageCount);
		CHECK(BackbufferImage(*target, *owner.rm, slot) == fake->NativeImage(frame % c_ImageCount));

		// An image the swapchain has not had back is undefined until a frame draws it.
		const auto expected =
			frame < c_ImageCount ? bgpu::BarrierLayout::kUndefined : bgpu::BarrierLayout::kPresent;
		CHECK(target->GetBackbufferLayout(slot) == expected);

		target->SetFrameFence(slot, 100 + frame);
		target->PresentAndAdvance();
		CHECK(target->GetLastPresentedIndex() == slot);
	}
	CHECK(log.presentedFences == std::vector<uint64_t>{ 100, 101, 102, 103, 104 });

	// A resize remakes the images, so none has been drawn and no slot waits on a fence.
	target->ResizeBackbuffers(16, 4);
	CHECK(log.resizes == 1);
	CHECK(target->GetFrameIndex() == 0);
	CHECK(target->GetFrameFence(0) == 0);
	CHECK(target->GetFrameFence(1) == 0);
	CHECK(target->GetBackbufferLayout(0) == bgpu::BarrierLayout::kUndefined);
	CHECK(BackbufferImage(*target, *owner.rm, 0) == fake->NativeImage(0));
	CHECK(owner.rm->GetTextureDesc(target->GetBackbufferTexture(0)).width == 16);
}

TEST_CASE("A headless swapchain target has no swapchain and can be captured", "[render][swapchain]")
{
	auto owner = Owner();

	auto desc     = bgl::RenderTargetDesc();
	desc.width    = 8;
	desc.height   = 8;
	desc.headless = true;

	auto target = core::SharedRef<bgl::RenderTarget>::Make(
		desc,
		nullptr,
		owner.device,
		owner.queue,
		owner.rm);

	CHECK(target->IsCapturable());

	// The ring is made in kCommon, and a frame leaves it in kPresent.
	CHECK(target->GetBackbufferLayout(0) == bgpu::BarrierLayout::kCommon);
	target->PresentAndAdvance();
	CHECK(target->GetFrameIndex() == 1);
	CHECK(target->GetBackbufferLayout(1) == bgpu::BarrierLayout::kCommon);
	target->PresentAndAdvance();
	CHECK(target->GetBackbufferLayout(0) == bgpu::BarrierLayout::kPresent);
}
TEST_CASE(
	"A swapchain target imports the images again when a present remakes them",
	"[render][swapchain]")
{
	auto owner = Owner();
	auto log   = FakeLog();

	auto desc   = bgl::RenderTargetDesc();
	desc.width  = 8;
	desc.height = 8;

	auto  swapchain = std::make_unique<FakeSwapchain>(*owner.rm, log, 8, 8);
	auto* fake      = swapchain.get();
	auto  target    = core::SharedRef<bgl::RenderTarget>::Make(
		desc,
		std::move(swapchain),
		owner.device,
		owner.queue,
		owner.rm);

	const bgpu::TextureHandle depth = target->GetDepthTexture();
	target->SetFrameFence(0, 7);
	target->PresentAndAdvance();
	target->SetFrameFence(1, 8);
	log.remakeAtNextPresent = true;
	target->PresentAndAdvance();

	// The ring starts over on the new images, none of them drawn, and the attachments are kept.
	CHECK(target->GetFrameIndex() == 0);
	CHECK(target->GetFrameFence(0) == 0);
	CHECK(target->GetFrameFence(1) == 0);
	CHECK_FALSE(target->HasPresented());
	CHECK(BackbufferImage(*target, *owner.rm, 0) == fake->NativeImage(0));
	CHECK(target->GetBackbufferLayout(0) == bgpu::BarrierLayout::kUndefined);
	CHECK(target->GetDepthTexture() == depth);
}
TEST_CASE(
	"A swapchain target takes on the size its window remade its images at",
	"[render][swapchain]")
{
	auto owner = Owner();
	auto log   = FakeLog();

	auto desc   = bgl::RenderTargetDesc();
	desc.width  = 8;
	desc.height = 8;

	auto target = core::SharedRef<bgl::RenderTarget>::Make(
		desc,
		std::make_unique<FakeSwapchain>(*owner.rm, log, 8, 8),
		owner.device,
		owner.queue,
		owner.rm);

	log.remakeAtNextPresent = true;
	log.remakeWidth         = 12;
	log.remakeHeight        = 6;
	target->PresentAndAdvance();

	// Every attachment follows, not only the backbuffers: a frame draws them all at one size.
	CHECK(target->GetWidth() == 12);
	CHECK(target->GetHeight() == 6);
	CHECK(owner.rm->GetTextureDesc(target->GetBackbufferTexture(0)).width == 12);
	CHECK(owner.rm->GetTextureDesc(target->GetDepthTexture()).width == 12);
	CHECK(owner.rm->GetTextureDesc(target->GetSceneColorTexture()).height == 6);
	CHECK(target->GetFrameIndex() == 0);
	CHECK(log.resizes == 0);
}
TEST_CASE(
	"A swapchain target resized takes the size its images were made at",
	"[render][swapchain]")
{
	auto owner = Owner();
	auto log   = FakeLog();

	auto desc   = bgl::RenderTargetDesc();
	desc.width  = 8;
	desc.height = 8;

	auto target = core::SharedRef<bgl::RenderTarget>::Make(
		desc,
		std::make_unique<FakeSwapchain>(*owner.rm, log, 8, 8),
		owner.device,
		owner.queue,
		owner.rm);

	// The caller asks for one size and the window is another, as during a drag.
	log.windowWidth  = 20;
	log.windowHeight = 10;
	target->ResizeBackbuffers(16, 4);

	CHECK(target->GetWidth() == 20);
	CHECK(target->GetHeight() == 10);
	CHECK(owner.rm->GetTextureDesc(target->GetSceneColorTexture()).width == 20);
	CHECK(owner.rm->GetTextureDesc(target->GetDepthTexture()).height == 10);
}
#endif
