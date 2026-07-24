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

	WGPUShaderModule
	MakeShaderModule(WGPUDevice device)
	{
		auto wgsl        = WGPUShaderSourceWGSL{};
		wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
		wgsl.code        = wgpu::ToStringView(c_ShaderSource);

		auto desc        = WGPUShaderModuleDescriptor{};
		desc.nextInChain = &wgsl.chain;

		return wgpuDeviceCreateShaderModule(device, &desc);
	}

	WGPURenderPipeline
	MakePipeline(WGPUDevice device, WGPUShaderModule module)
	{
		auto colorTarget      = WGPUColorTargetState{};
		colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
		colorTarget.writeMask = WGPUColorWriteMask_All;

		auto fragment        = WGPUFragmentState{};
		fragment.module      = module;
		fragment.entryPoint  = wgpu::ToStringView("fs_main");
		fragment.targetCount = 1;
		fragment.targets     = &colorTarget;

		auto desc               = WGPURenderPipelineDescriptor{};
		desc.vertex.module      = module;
		desc.vertex.entryPoint  = wgpu::ToStringView("vs_main");
		desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
		desc.multisample.count  = 1;
		desc.multisample.mask   = 0xFFFFFFFF;
		desc.fragment           = &fragment;

		return wgpuDeviceCreateRenderPipeline(device, &desc);
	}

	// Renders the triangle into a fresh texture and returns its pixels, row-major from the top.
	std::vector<Rgba>
	RenderTriangle(wgpu::Device& device)
	{
		WGPUDevice   handle   = device.GetHandle();
		WGPUQueue    queue    = device.GetQueue();
		WGPUInstance instance = device.GetInstance();

		auto textureDesc          = WGPUTextureDescriptor{};
		textureDesc.dimension     = WGPUTextureDimension_2D;
		textureDesc.size          = { c_Size, c_Size, 1 };
		textureDesc.format        = WGPUTextureFormat_RGBA8Unorm;
		textureDesc.mipLevelCount = 1;
		textureDesc.sampleCount   = 1;
		textureDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;

		WGPUTexture     texture = wgpuDeviceCreateTexture(handle, &textureDesc);
		WGPUTextureView view    = wgpuTextureCreateView(texture, nullptr);

		auto bufferDesc  = WGPUBufferDescriptor{};
		bufferDesc.size  = uint64_t{ c_BytesPerRow } * c_Size;
		bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;

		WGPUBuffer readback = wgpuDeviceCreateBuffer(handle, &bufferDesc);

		WGPUShaderModule   module   = MakeShaderModule(handle);
		WGPURenderPipeline pipeline = MakePipeline(handle, module);

		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle, nullptr);

		auto attachment       = WGPURenderPassColorAttachment{};
		attachment.view       = view;
		attachment.loadOp     = WGPULoadOp_Clear;
		attachment.storeOp    = WGPUStoreOp_Store;
		attachment.clearValue = { 1.0, 0.0, 0.0, 1.0 };
		attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

		auto passDesc                 = WGPURenderPassDescriptor{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments     = &attachment;

		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
		wgpuRenderPassEncoderSetPipeline(pass, pipeline);
		wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
		wgpuRenderPassEncoderEnd(pass);
		wgpuRenderPassEncoderRelease(pass);

		auto source    = WGPUTexelCopyTextureInfo{};
		source.texture = texture;
		source.aspect  = WGPUTextureAspect_All;

		auto destination                = WGPUTexelCopyBufferInfo{};
		destination.buffer              = readback;
		destination.layout.bytesPerRow  = c_BytesPerRow;
		destination.layout.rowsPerImage = c_Size;

		const auto extent = WGPUExtent3D{ c_Size, c_Size, 1 };
		wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);

		WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
		wgpuQueueSubmit(queue, 1, &commands);

		auto mapStatus = WGPUMapAsyncStatus_Error;

		auto mapInfo      = WGPUBufferMapCallbackInfo{};
		mapInfo.mode      = WGPUCallbackMode_WaitAnyOnly;
		mapInfo.userdata1 = &mapStatus;
		mapInfo.callback  = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
			*static_cast<WGPUMapAsyncStatus*>(userdata) = status;
		};

		auto wait   = WGPUFutureWaitInfo{};
		wait.future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bufferDesc.size, mapInfo);
		wgpuInstanceWaitAny(instance, 1, &wait, UINT64_MAX);

		auto pixels = std::vector<Rgba>();

		if (mapStatus == WGPUMapAsyncStatus_Success)
		{
			const auto* mapped = static_cast<const Rgba*>(
				wgpuBufferGetConstMappedRange(readback, 0, bufferDesc.size));

			pixels.assign(mapped, mapped + (c_Size * c_Size));
			wgpuBufferUnmap(readback);
		}

		wgpuCommandBufferRelease(commands);
		wgpuCommandEncoderRelease(encoder);
		wgpuRenderPipelineRelease(pipeline);
		wgpuShaderModuleRelease(module);
		wgpuBufferRelease(readback);
		wgpuTextureViewRelease(view);
		wgpuTextureRelease(texture);

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
	auto device = wgpu::Device(wgpu::DeviceDesc{});

	const auto pixels = RenderTriangle(device);

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
