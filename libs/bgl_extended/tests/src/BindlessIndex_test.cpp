#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputePipeline.h"
#include "resource/ResourceManager.h"
#include "uniforms/Uniforms.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"

namespace
{
	template <typename T>
	concept HasBindlessIndex = requires(T h) { h.bindlessIndex; };

	template <typename T>
	concept HasResidentDescriptor = requires(T h) { h.descriptor; };

	// The first uint32 of the descriptor handle a member holds, straight out of the flat mirror --
	// which is the only view of it, since an accessor refuses to read a handle field back as a
	// value.
	uint32_t
	ReadHandleField(const bgl::Uniforms& uniforms, std::string_view member)
	{
		uint32_t written = 0;
		std::memcpy(
			&written,
			static_cast<const std::byte*>(uniforms.Data()) + uniforms[member].GetOffset(),
			sizeof(written));
		return written;
	}
}

// A texture names storage; a view names how a shader reaches it. Only the view carries the numbers,
// so a texture that is never sampled has no bindless index to be wrong about.
TEST_CASE("Only a view carries a bindless index", "[uniforms][bindless]")
{
	STATIC_REQUIRE(HasBindlessIndex<bgl::SrvHandle>);
	STATIC_REQUIRE(HasResidentDescriptor<bgl::SrvHandle>);
	STATIC_REQUIRE_FALSE(HasBindlessIndex<bgl::TextureHandle>);

	SECTION("an asset handle still names its texture")
	{
		const auto asset = bgl::TextureAssetHandle{ core::slot_handle{ 7u, 3u }, 1007u };

		CHECK(bgl::TextureHandle::From(asset).slot.index == 7u);
	}
}

// The migration's real gate. Both numbers are still equal everywhere, so no runtime assertion can
// tell a slot from a descriptor -- the compiler can, once the conversion is gone.
TEST_CASE("A slot cannot be spelled as a descriptor", "[uniforms][bindless]")
{
	STATIC_REQUIRE_FALSE(std::is_constructible_v<bgl::DescriptorHandle, core::slot_handle>);

	// The index off a handle is still a plain uint32_t, and still is one.
	STATIC_REQUIRE(std::is_constructible_v<bgl::DescriptorHandle, uint32_t>);
}

TEST_CASE("Uniforms write a handle's bindless index", "[uniforms][bindless]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto device = gfxBase->GetDevice();
	REQUIRE(device != nullptr);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));

	SECTION("the index, not the slot")
	{
		// Never created through the resource manager: nothing between the accessor and the mirror
		// validates a handle, so a fabricated one with the two numbers deliberately apart is enough
		// to observe which of them gets written.
		kernel["gUniforms"]["outBuffer"] = bgl::BufferHandle{ core::slot_handle{ 7u, 3u }, 1007u };

		CHECK(ReadHandleField(kernel["gUniforms"], "outBuffer") == 1007u);
	}

	SECTION("and the resource manager stamps one on every buffer it hands out")
	{
		auto resourceManager = gfxBase->GetResourceManagerCpy();
		REQUIRE(resourceManager != nullptr);

		auto bufDesc = bgl::ComputeBufferDesc();
		bufDesc.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("Bindless Index Buffer");

		const bgl::BufferHandle buffer = resourceManager->CreateComputeBuffer(bufDesc);
		REQUIRE(resourceManager->ValidBufferHandle(buffer));

		CHECK(buffer.bindlessIndex != core::slot_handle::invalid_index);

		kernel["gUniforms"]["outBuffer"] = buffer;

		CHECK(ReadHandleField(kernel["gUniforms"], "outBuffer") == buffer.bindlessIndex);

		resourceManager->DestroyBuffer(buffer);
	}

	// The mirror is zero-filled, so a handle field nobody wrote reads index 0. If anything were
	// allocated there, "never bound" and "bound to the first resource the manager handed out" would
	// be the same eight bytes -- and the shader would sample a live resource instead of nothing.
	// A fresh manager, not the graphics one: on that one the scene's own buffers have already
	// consumed the low indices, so the very first allocation is what has to be checked. Without the
	// reservation this manager would hand index 0 straight out and the check would fail.
	SECTION("and never hands the sentinel index to a real resource")
	{
		auto resourceManager = device->CreateResourceManager(bgl::ResourceManagerDesc());
		REQUIRE(resourceManager != nullptr);

		auto bufDesc = bgl::ComputeBufferDesc();
		bufDesc.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("Sentinel Probe");

		const bgl::BufferHandle first = resourceManager->CreateComputeBuffer(bufDesc);
		REQUIRE(resourceManager->ValidBufferHandle(first));

		CHECK(first.bindlessIndex != bgl::c_UnboundDescriptorIndex);

		resourceManager->DestroyBuffer(first);
	}
}
