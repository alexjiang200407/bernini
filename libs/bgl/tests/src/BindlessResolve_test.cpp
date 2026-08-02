#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <core/ref/RefCounter.h>

namespace
{
	constexpr uint32_t c_ResolveBias = 1000u;

	// A backend whose resolve is deliberately not the identity. Every real backend today answers
	// with the handle's own slot, so a mirror that wrote the slot verbatim would pass against
	// either of them; against this one it cannot.
	class BiasedUniforms final : public core::RefCounter<bgl::IUniforms>
	{
	public:
		using core::RefCounter<bgl::IUniforms>::RefCounter;

		[[nodiscard]] bgl::DescriptorHandle
		ResolveBindless(const bgl::BufferHandle& handle) const noexcept override
		{
			return bgl::DescriptorHandle(handle.slot.index + c_ResolveBias);
		}

		[[nodiscard]] bgl::DescriptorHandle
		ResolveBindless(const bgl::SamplerHandle& handle) const noexcept override
		{
			return bgl::DescriptorHandle(handle.idx + c_ResolveBias);
		}

		[[nodiscard]] bgl::DescriptorHandle
		ResolveBindless(const bgl::TextureHandle& handle) const noexcept override
		{
			return bgl::DescriptorHandle(handle.slot.index + c_ResolveBias);
		}
	};

	// The first uint32 of the descriptor handle a member holds, straight out of the flat mirror --
	// which is the only view of it, since an accessor refuses to read a handle field back as a
	// value.
	uint32_t
	ReadHandleField(const bgl::IUniforms& uniforms, std::string_view member)
	{
		uint32_t written = 0;
		std::memcpy(
			&written,
			static_cast<const std::byte*>(uniforms.Data()) + uniforms[member].GetOffset(),
			sizeof(written));
		return written;
	}
}

// A bindless handle assigned through an accessor must reach the constant buffer as whatever the
// backend's ResolveBindless answers, never as the handle's own slot index. The two coincide today
// -- a pool slot is its descriptor index on D3D12, and the value Metal patches at dispatch -- and
// stop coinciding the moment D3D12 hands descriptors out through an allocator, which is what this
// pins ahead of that change.
TEST_CASE("Uniforms write the resolved bindless handle", "[uniforms][bindless]")
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

	auto pipeline = device->CreateComputePipeline(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));
	REQUIRE(pipeline != nullptr);

	// Never created through the resource manager: nothing between the accessor and the mirror
	// validates a handle, so a fabricated slot is enough to observe what gets written.
	const auto handle = bgl::BufferHandle{ core::slot_handle{ 7u, 3u } };

	SECTION("the accessor goes through the backend hook")
	{
		auto  ref      = core::SharedRef<BiasedUniforms>::Make(pipeline.Get(), "gUniforms");
		auto& uniforms = *ref.Get();

		uniforms["outBuffer"] = handle;

		CHECK(ReadHandleField(uniforms, "outBuffer") == handle.slot.index + c_ResolveBias);
	}

	SECTION("the backend in this build resolves a buffer to its slot")
	{
		auto  ref      = device->CreateUniforms(pipeline.Get(), "gUniforms");
		auto& uniforms = *ref.Get();

		uniforms["outBuffer"] = handle;

		CHECK(ReadHandleField(uniforms, "outBuffer") == handle.slot.index);
	}
}
