#include "device/Device_wgpu.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The first raster path through the backend: a render pipeline, a render pass and a draw,
// rendered offscreen and read back so the result is asserted rather than looked at. Nothing here
// goes through CreateGraphics -- RenderContext eagerly builds mesh-shader pipelines, which WebGPU
// does not have, so the public API cannot come up yet.

namespace
{
	// 64 x RGBA8 is exactly 256 bytes, which is the row alignment a texture-to-buffer copy
	// requires -- so the readback needs no padded-row handling to be correct.
	constexpr uint32_t c_Size        = 64;
	constexpr uint32_t c_BytesPerRow = c_Size * 4;

	constexpr auto c_ShaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> @builtin(position) vec4<f32> {
    var corners = array<vec2<f32>, 3>(
        vec2<f32>( 0.0,  0.8),
        vec2<f32>(-0.8, -0.8),
        vec2<f32>( 0.8, -0.8),
    );
    return vec4<f32>(corners[index], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(0.0, 1.0, 0.0, 1.0);
}
)";

	struct Rgba
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;

		bool
		operator==(const Rgba&) const = default;
	};

	wgpu::ShaderModule
	MakeShaderModule(const wgpu::Device& device)
	{
		auto wgsl = wgpu::ShaderSourceWGSL{};
		wgsl.code = c_ShaderSource;

		auto desc        = wgpu::ShaderModuleDescriptor{};
		desc.nextInChain = &wgsl;

		return device.CreateShaderModule(&desc);
	}

	wgpu::RenderPipeline
	MakePipeline(const wgpu::Device& device, const wgpu::ShaderModule& module)
	{
		auto colorTarget      = wgpu::ColorTargetState{};
		colorTarget.format    = wgpu::TextureFormat::RGBA8Unorm;
		colorTarget.writeMask = wgpu::ColorWriteMask::All;

		auto fragment        = wgpu::FragmentState{};
		fragment.module      = module;
		fragment.entryPoint  = "fs_main";
		fragment.targetCount = 1;
		fragment.targets     = &colorTarget;

		auto desc               = wgpu::RenderPipelineDescriptor{};
		desc.vertex.module      = module;
		desc.vertex.entryPoint  = "vs_main";
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.multisample.count  = 1;
		desc.multisample.mask   = 0xFFFFFFFF;
		desc.fragment           = &fragment;

		return device.CreateRenderPipeline(&desc);
	}

	// Renders the triangle into a fresh texture and returns its pixels, row-major from the top.
	std::vector<Rgba>
	RenderTriangle(Device& device)
	{
		const wgpu::Device&   handle   = device.GetHandle();
		const wgpu::Queue&    queue    = device.GetQueue();
		const wgpu::Instance& instance = device.GetInstance();

		auto textureDesc          = wgpu::TextureDescriptor{};
		textureDesc.dimension     = wgpu::TextureDimension::e2D;
		textureDesc.size          = { c_Size, c_Size, 1 };
		textureDesc.format        = wgpu::TextureFormat::RGBA8Unorm;
		textureDesc.mipLevelCount = 1;
		textureDesc.sampleCount   = 1;
		textureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;

		wgpu::Texture     texture = handle.CreateTexture(&textureDesc);
		wgpu::TextureView view    = texture.CreateView();

		auto bufferDesc  = wgpu::BufferDescriptor{};
		bufferDesc.size  = uint64_t{ c_BytesPerRow } * c_Size;
		bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;

		wgpu::Buffer readback = handle.CreateBuffer(&bufferDesc);

		wgpu::ShaderModule   module   = MakeShaderModule(handle);
		wgpu::RenderPipeline pipeline = MakePipeline(handle, module);

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
		pass.SetPipeline(pipeline);
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
}

TEST_CASE("A triangle rasterizes offscreen and reads back", "[wgpu][render]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	const auto pixels = RenderTriangle(*device);

	REQUIRE(pixels.size() == c_Size * c_Size);

	constexpr auto c_Green = Rgba{ 0, 255, 0, 255 };
	constexpr auto c_Red   = Rgba{ 255, 0, 0, 255 };

	SECTION("the triangle covers the centre")
	{
		REQUIRE(At(pixels, c_Size / 2, c_Size / 2) == c_Green);
	}

	SECTION("the clear colour survives where the triangle is not")
	{
		// Every corner is outside the triangle, so a pass that never ran -- or one that cleared
		// over its own output -- cannot produce this alongside a green centre.
		REQUIRE(At(pixels, 0, 0) == c_Red);
		REQUIRE(At(pixels, c_Size - 1, 0) == c_Red);
		REQUIRE(At(pixels, 0, c_Size - 1) == c_Red);
		REQUIRE(At(pixels, c_Size - 1, c_Size - 1) == c_Red);
	}

	SECTION("the triangle is bottom-heavy, matching its winding")
	{
		// The apex is at the top in NDC, which is row 0 after the Y flip, so the covered span
		// must widen going down the image. This is what catches an inverted viewport.
		const auto rowCoverage = [&](uint32_t y) {
			auto covered = 0;
			for (uint32_t x = 0; x < c_Size; ++x) covered += At(pixels, x, y) == c_Green ? 1 : 0;
			return covered;
		};

		REQUIRE(rowCoverage(c_Size / 4) < rowCoverage(c_Size * 3 / 4));
	}
}
