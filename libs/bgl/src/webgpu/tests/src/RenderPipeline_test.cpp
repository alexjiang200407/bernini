#include "device/Device_wgpu.h"
#include "pipeline/RenderPipeline_wgpu.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The render pipeline built from a shared Slang source: VSMain/PSMain compile to one WGSL module,
// reflect (no resources here), and build a wgpu::RenderPipeline over an RGBA8 target. The triangle
// is drawn offscreen and read back, so the pipeline object is asserted on the pixels it produces --
// not merely that CreateRenderPipeline returned. Complements Triangle_test, which drives raw WGSL.

namespace
{
	constexpr uint32_t c_Size        = 64;
	constexpr uint32_t c_BytesPerRow = c_Size * 4;

	struct Rgba
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;

		bool
		operator==(const Rgba&) const = default;
	};

	std::vector<Rgba>
	DrawTriangle(Device& device, const RenderPipeline& pipeline)
	{
		const wgpu::Device&   handle   = device.GetHandle();
		const wgpu::Queue&    queue    = device.GetQueue();
		const wgpu::Instance& instance = device.GetInstance();

		auto textureDesc      = wgpu::TextureDescriptor{};
		textureDesc.dimension = wgpu::TextureDimension::e2D;
		textureDesc.size      = { c_Size, c_Size, 1 };
		textureDesc.format    = wgpu::TextureFormat::RGBA8Unorm;
		textureDesc.usage     = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;

		wgpu::Texture     texture = handle.CreateTexture(&textureDesc);
		wgpu::TextureView view    = texture.CreateView();

		auto bufferDesc  = wgpu::BufferDescriptor{};
		bufferDesc.size  = uint64_t{ c_BytesPerRow } * c_Size;
		bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;

		wgpu::Buffer readback = handle.CreateBuffer(&bufferDesc);

		wgpu::CommandEncoder encoder = handle.CreateCommandEncoder();

		auto attachment       = wgpu::RenderPassColorAttachment{};
		attachment.view       = view;
		attachment.loadOp     = wgpu::LoadOp::Clear;
		attachment.storeOp    = wgpu::StoreOp::Store;
		attachment.clearValue = { 1.0, 0.0, 0.0, 1.0 };

		auto passDesc                 = wgpu::RenderPassDescriptor{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments     = &attachment;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		pass.SetPipeline(pipeline.GetPipeline());
		pass.Draw(3);
		pass.End();

		auto source    = wgpu::TexelCopyTextureInfo{};
		source.texture = texture;
		source.aspect  = wgpu::TextureAspect::All;

		auto destination                = wgpu::TexelCopyBufferInfo{};
		destination.buffer              = readback;
		destination.layout.bytesPerRow  = c_BytesPerRow;
		destination.layout.rowsPerImage = c_Size;

		const auto extent = wgpu::Extent3D{ c_Size, c_Size, 1 };
		encoder.CopyTextureToBuffer(&source, &destination, &extent);

		wgpu::CommandBuffer commands = encoder.Finish();
		queue.Submit(1, &commands);

		auto status = wgpu::MapAsyncStatus::Error;

		const auto future = readback.MapAsync(
			wgpu::MapMode::Read,
			0,
			bufferDesc.size,
			wgpu::CallbackMode::WaitAnyOnly,
			[&status](wgpu::MapAsyncStatus s, wgpu::StringView) { status = s; });

		instance.WaitAny(future, UINT64_MAX);

		auto pixels = std::vector<Rgba>();
		if (status == wgpu::MapAsyncStatus::Success)
		{
			const auto* mapped =
				static_cast<const Rgba*>(readback.GetConstMappedRange(0, bufferDesc.size));
			pixels.assign(mapped, mapped + (c_Size * c_Size));
			readback.Unmap();
		}
		return pixels;
	}

	Rgba
	At(const std::vector<Rgba>& pixels, uint32_t x, uint32_t y)
	{
		return pixels[(y * c_Size) + x];
	}

	RenderPipelineDesc
	TriangleDesc(Device& device)
	{
		auto desc   = RenderPipelineDesc{};
		desc.shader = device.CreateShader(ShaderDesc{}.SetSlangModuleName("RasterTriangleTest"));
		desc.vertexEntry = "VSMain";
		desc.pixelEntry  = "PSMain";
		desc.rtvFormats.push_back(Format::RGBA8_UNORM);
		// The clip-space triangle is wound for neither convention in particular; culling is not what
		// this exercises, so draw both faces.
		desc.renderState.rasterState.SetCullNone();
		desc.debugName = "triangle";
		return desc;
	}
}

TEST_CASE("A render pipeline from Slang rasterizes a triangle", "[wgpu][render]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	auto pipeline =
		RenderPipeline(device->GetHandle(), device->GetSlangSession(), TriangleDesc(*device));

	const auto pixels = DrawTriangle(*device, pipeline);
	REQUIRE(pixels.size() == c_Size * c_Size);

	constexpr auto c_Green = Rgba{ 0, 255, 0, 255 };
	constexpr auto c_Red   = Rgba{ 255, 0, 0, 255 };

	SECTION("the triangle covers the centre and the clear survives the corners")
	{
		REQUIRE(At(pixels, c_Size / 2, c_Size / 2) == c_Green);
		REQUIRE(At(pixels, 0, 0) == c_Red);
		REQUIRE(At(pixels, c_Size - 1, c_Size - 1) == c_Red);
	}

	SECTION("a real triangle is drawn, not a full-target fill")
	{
		uint32_t green = 0;
		for (const Rgba& p : pixels) green += (p == c_Green) ? 1u : 0u;

		// A centred triangle covers a meaningful fraction, but nowhere near the whole target -- so
		// this fails both if the draw was dropped (0) and if the pipeline somehow filled everything.
		REQUIRE(green > (c_Size * c_Size) / 10);
		REQUIRE(green < (c_Size * c_Size) * 3 / 4);
	}
}

TEST_CASE(
	"A render pipeline with a depth target builds without a validation error",
	"[wgpu][render]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	const wgpu::Device& handle = device->GetHandle();
	handle.PushErrorScope(wgpu::ErrorFilter::Validation);

	auto desc      = TriangleDesc(*device);
	desc.dsvFormat = Format::D32;
	desc.renderState.depthStencilState.EnableDepthTest().EnableDepthWrite().SetDepthFunc(
		ComparisonFunc::kLessOrEqual);

	auto pipeline = RenderPipeline(device->GetHandle(), device->GetSlangSession(), desc);

	auto       error  = std::string();
	const auto future = handle.PopErrorScope(
		wgpu::CallbackMode::WaitAnyOnly,
		[&error](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
			if (type != wgpu::ErrorType::NoError)
				error = std::string(std::string_view(message));
		});
	device->GetInstance().WaitAny(future, UINT64_MAX);

	INFO("Dawn: " << error);
	REQUIRE(error.empty());
	REQUIRE(pipeline.GetPipeline() != nullptr);
}
