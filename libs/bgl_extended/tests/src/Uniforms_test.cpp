#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/Sampler.h"
#include "resource/Srv.h"
#include "uniforms/DescriptorHandle.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl_common/UniformValueType.h>
#include <bgl_common/UniformsBase.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	// A 2-component vector following a scalar sits at a different offset per target, and both are
	// right: HLSL's scalar layout packs it at the next 4 bytes, MSL aligns it to its own size, 8.
	// The reflection reports what the emitted shader actually does, so the expectation follows it.
#if defined(RENDERER_BACKEND_METAL)
	constexpr uint32_t c_Vec2AlignPad = 4u;
#else
	constexpr uint32_t c_Vec2AlignPad = 0u;
#endif
}

TEST_CASE("Uniforms", "[uniforms]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;
	opts.logLevel                 = bgl::GraphicsOptions::LogLevel::kTrace;

	auto gfx = bgl::CreateGraphics(opts);

	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();

	REQUIRE(gfxBase != nullptr);

	auto device = gfxBase->GetDevice();

	REQUIRE(device != nullptr);

	SECTION("Scalar")
	{
		auto pipelineDesc = bgl::MeshletPipelineDesc();

		pipelineDesc.SetMeshShader(device->CreateShader("MSUniformReflectionScalar"));

		auto pipeline = device->CreateMeshletPipeline(pipelineDesc);
		auto uniforms = device->CreateUniforms(pipeline, "gUniforms");

		CHECK(uniforms.GetSize() == 208u);

		// f1
		{
			CHECK(uniforms["f1"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["f1"].GetValueType() == bgl::UniformValueType::kFloat);
			CHECK(uniforms["f1"].GetOffset() == 0u);

			// Is scalar, so no indexing or member access allowed
			CHECK(uniforms["f1"][0].IsNull());
			CHECK(uniforms["f1"]["a"s].IsNull());

			// Type mismatch
			// CHECK_THROWS(static_cast<float>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::vec2>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::vec3>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::vec4>(uniforms["f1"]));
			CHECK_THROWS(static_cast<uint32_t>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::uvec2>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::uvec3>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::uvec4>(uniforms["f1"]));
			CHECK_THROWS(static_cast<int32_t>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::ivec2>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::ivec3>(uniforms["f1"]));
			CHECK_THROWS(static_cast<glm::ivec4>(uniforms["f1"]));
			CHECK_THROWS(static_cast<bool>(uniforms["f1"]));
			CHECK_THROWS(uniforms["f1"].operator glm::mat4());

			//CHECK_THROWS(uniforms["f1"] = 0.0f);
			CHECK_THROWS(uniforms["f1"] = glm::vec2(1.0f));
			CHECK_THROWS(uniforms["f1"] = glm::vec3(1.0f));
			CHECK_THROWS(uniforms["f1"] = glm::vec4(1.0f));
			CHECK_THROWS(uniforms["f1"] = 1);
			CHECK_THROWS(uniforms["f1"] = glm::ivec2(42));
			CHECK_THROWS(uniforms["f1"] = glm::ivec3(42));
			CHECK_THROWS(uniforms["f1"] = glm::ivec4(42));
			CHECK_THROWS(uniforms["f1"] = 1u);
			CHECK_THROWS(uniforms["f1"] = glm::uvec2(42u));
			CHECK_THROWS(uniforms["f1"] = glm::uvec3(42u));
			CHECK_THROWS(uniforms["f1"] = glm::uvec4(42u));
			CHECK_THROWS(uniforms["f1"] = glm::mat4(1.0f));

			CHECK_NOTHROW(uniforms["f1"] = 420.0f);
			CHECK(uniforms["f1"] == 420.0f);
		}

		// f2
		{
			CHECK(uniforms["f2"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["f2"].GetValueType() == bgl::UniformValueType::kFloat2);
			CHECK(uniforms["f2"].GetOffset() == 4u + c_Vec2AlignPad);  // f1 (0) + 4 bytes

			CHECK(uniforms["f2"][0].IsNull());
			CHECK(uniforms["f2"]["a"s].IsNull());

			CHECK_THROWS(static_cast<float>(uniforms["f2"]));
			CHECK_THROWS(static_cast<glm::vec3>(uniforms["f2"]));
			CHECK_THROWS(static_cast<glm::uvec2>(uniforms["f2"]));

			CHECK_THROWS(uniforms["f2"] = 1.0f);
			CHECK_THROWS(uniforms["f2"] = glm::vec3(1.0f));

			CHECK_NOTHROW(uniforms["f2"] = glm::vec2(1.0f, 2.0f));
			CHECK(uniforms["f2"] == glm::vec2(1.0f, 2.0f));
		}

		// f3
		{
			CHECK(uniforms["f3"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["f3"].GetValueType() == bgl::UniformValueType::kFloat3);
			CHECK(uniforms["f3"].GetOffset() == 16u);  // Aligns to next 16-byte boundary

			CHECK(uniforms["f3"][0].IsNull());
			CHECK_THROWS(static_cast<glm::vec4>(uniforms["f3"]));
			CHECK_THROWS(uniforms["f3"] = glm::vec4(1.0f));

			CHECK_NOTHROW(uniforms["f3"] = glm::vec3(1.0f, 2.0f, 3.0f));
			CHECK(uniforms["f3"] == glm::vec3(1.0f, 2.0f, 3.0f));
		}

		// f4
		{
			CHECK(uniforms["f4"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["f4"].GetValueType() == bgl::UniformValueType::kFloat4);
			CHECK(uniforms["f4"].GetOffset() == 32u);  // Row 3 start

			CHECK_THROWS(static_cast<glm::vec3>(uniforms["f4"]));
			CHECK_THROWS(uniforms["f4"] = glm::vec3(1.0f));

			CHECK_NOTHROW(uniforms["f4"] = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
			CHECK(uniforms["f4"] == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
		}

		// i1
		{
			CHECK(uniforms["i1"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["i1"].GetValueType() == bgl::UniformValueType::kInt);
			CHECK(uniforms["i1"].GetOffset() == 48u);  // Row 4 start

			CHECK_THROWS(static_cast<int32_t>(uniforms["f1"]));  // Cross-contamination test
			CHECK_THROWS(static_cast<glm::ivec2>(uniforms["i1"]));
			CHECK_THROWS(uniforms["i1"] = glm::ivec2(1));

			CHECK_NOTHROW(uniforms["i1"] = -42);
			CHECK(uniforms["i1"] == -42);
		}

		// i2
		{
			CHECK(uniforms["i2"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["i2"].GetValueType() == bgl::UniformValueType::kInt2);
			CHECK(uniforms["i2"].GetOffset() == 52u + c_Vec2AlignPad);

			CHECK_THROWS(static_cast<int32_t>(uniforms["i2"]));
			CHECK_THROWS(uniforms["i2"] = 1);

			CHECK_NOTHROW(uniforms["i2"] = glm::ivec2(-1, -2));
			CHECK(uniforms["i2"] == glm::ivec2(-1, -2));
		}

		// i3
		{
			CHECK(uniforms["i3"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["i3"].GetValueType() == bgl::UniformValueType::kInt3);
			CHECK(uniforms["i3"].GetOffset() == 64u);  // Aligns to next 16-byte boundary

			CHECK_THROWS(static_cast<glm::ivec4>(uniforms["i3"]));
			CHECK_THROWS(uniforms["i3"] = glm::ivec4(1));

			CHECK_NOTHROW(uniforms["i3"] = glm::ivec3(-1, -2, -3));
			CHECK(uniforms["i3"] == glm::ivec3(-1, -2, -3));
		}

		// i4
		{
			CHECK(uniforms["i4"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["i4"].GetValueType() == bgl::UniformValueType::kInt4);
			CHECK(uniforms["i4"].GetOffset() == 80u);

			CHECK_THROWS(static_cast<glm::ivec3>(uniforms["i4"]));
			CHECK_THROWS(uniforms["i4"] = glm::ivec3(1));

			CHECK_NOTHROW(uniforms["i4"] = glm::ivec4(-1, -2, -3, -4));
			CHECK(uniforms["i4"] == glm::ivec4(-1, -2, -3, -4));
		}

		// u1
		{
			CHECK(uniforms["u1"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["u1"].GetValueType() == bgl::UniformValueType::kUInt);
			CHECK(uniforms["u1"].GetOffset() == 96u);  // Row 7 start

			CHECK_THROWS(static_cast<glm::uvec2>(uniforms["u1"]));
			CHECK_THROWS(uniforms["u1"] = glm::uvec2(1u));

			CHECK_NOTHROW(uniforms["u1"] = 42u);
			CHECK(uniforms["u1"] == 42u);
		}

		// u2
		{
			CHECK(uniforms["u2"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["u2"].GetValueType() == bgl::UniformValueType::kUInt2);
			CHECK(uniforms["u2"].GetOffset() == 100u + c_Vec2AlignPad);

			CHECK_THROWS(static_cast<uint32_t>(uniforms["u2"]));
			CHECK_THROWS(uniforms["u2"] = 1u);

			CHECK_NOTHROW(uniforms["u2"] = glm::uvec2(1u, 2u));
			CHECK(uniforms["u2"] == glm::uvec2(1u, 2u));
		}

		// u3
		{
			CHECK(uniforms["u3"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["u3"].GetValueType() == bgl::UniformValueType::kUInt3);
			CHECK(uniforms["u3"].GetOffset() == 112u);  // Aligns to next 16-byte boundary

			CHECK_THROWS(static_cast<glm::uvec4>(uniforms["u3"]));
			CHECK_THROWS(uniforms["u3"] = glm::uvec4(1u));

			CHECK_NOTHROW(uniforms["u3"] = glm::uvec3(1u, 2u, 3u));
			CHECK(uniforms["u3"] == glm::uvec3(1u, 2u, 3u));
		}

		// u4
		{
			CHECK(uniforms["u4"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["u4"].GetValueType() == bgl::UniformValueType::kUInt4);
			CHECK(uniforms["u4"].GetOffset() == 128u);

			CHECK_THROWS(static_cast<glm::uvec3>(uniforms["u4"]));
			CHECK_THROWS(uniforms["u4"] = glm::uvec3(1u));

			CHECK_NOTHROW(uniforms["u4"] = glm::uvec4(1u, 2u, 3u, 4u));
			CHECK(uniforms["u4"] == glm::uvec4(1u, 2u, 3u, 4u));
		}

		// mat
		{
			CHECK(uniforms["mat"].GetType() == bgl::UniformType::kValue);
			CHECK(uniforms["mat"].GetValueType() == bgl::UniformValueType::kMat4x4);
			CHECK(uniforms["mat"].GetOffset() == 144u);  // Row 10 start

			CHECK(uniforms["mat"][0].IsNull());
			CHECK_THROWS(static_cast<glm::vec4>(uniforms["mat"]));
			CHECK_THROWS(uniforms["mat"] = glm::vec4(1.0f));

			glm::mat4 testMatrix = glm::mat4(
				2.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				2.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				2.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				2.0f);

			// Invoke explicit conversion via named operator method to circumvent GLM constructor ambiguity
			CHECK_NOTHROW(uniforms["mat"] = testMatrix);
			CHECK(uniforms["mat"].operator glm::mat4() == testMatrix);
		}
	}

	SECTION("Array") {}

	SECTION("Struct") {}
}

// Binding cannot tell a member this PSO variant omits from a member that no longer exists: both
// leave the accessor invalid. Resolving the binder's names against the whole family once is what
// separates them, so a shader rename fails loudly instead of silently binding nothing.
TEST_CASE("A member no PSO variant declares is reported", "[uniforms]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto device = gfx->As<bgl::GraphicsBase>()->GetDevice();
	REQUIRE(device != nullptr);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));

	const bgl::Uniforms* variants[] = { &kernel["gUniforms"] };

	SECTION("a declared member resolves")
	{
		CHECK(kernel["gUniforms"].HasMember("outBuffer"));
		CHECK_FALSE(kernel["gUniforms"].HasMember("noSuchMember"));
	}

	SECTION("only the undeclared name comes back")
	{
		constexpr std::array c_Names = { "outBuffer"sv, "noSuchMember"sv };

		const auto unknown = bgl::FindUnknownMembers(variants, c_Names);

		REQUIRE(unknown.size() == 1);
		CHECK(unknown.front() == "noSuchMember"sv);
	}

	SECTION("a variant that does not declare the buffer at all is skipped, not counted against it")
	{
		const bgl::Uniforms* withAbsent[] = { nullptr, &kernel["gUniforms"] };
		constexpr std::array c_Names      = { "outBuffer"sv };

		CHECK(bgl::FindUnknownMembers(withAbsent, c_Names).empty());
	}
}

TEST_CASE("An optional uniform write skips a member the shader does not declare", "[uniforms]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto device = gfx->As<bgl::GraphicsBase>()->GetDevice();
	REQUIRE(device != nullptr);

	SECTION("a value")
	{
		auto pipelineDesc = bgl::MeshletPipelineDesc();
		pipelineDesc.SetMeshShader(device->CreateShader("MSUniformReflectionScalar"));

		auto pipeline = device->CreateMeshletPipeline(pipelineDesc);
		auto uniforms = device->CreateUniforms(pipeline, "gUniforms");

		uniforms["f1"].SetIfValid(1.5f);
		CHECK(static_cast<float>(uniforms["f1"]) == 1.5f);

		// The whole mirror, so a stray write anywhere in it is caught and not just one at f1's
		// offset -- an absent member resolves to offset 0, which is where f1 lives.
		const auto before = std::vector<std::byte>(
			static_cast<const std::byte*>(uniforms.Data()),
			static_cast<const std::byte*>(uniforms.Data()) + uniforms.GetSize());

		CHECK_NOTHROW(uniforms["noSuchMember"].SetIfValid(2.5f));

		const auto after = std::vector<std::byte>(
			static_cast<const std::byte*>(uniforms.Data()),
			static_cast<const std::byte*>(uniforms.Data()) + uniforms.GetSize());

		CHECK(before == after);

		// The unguarded form is what a required member uses, and the same name still throws
		// there -- SetIfValid opts out of that, it does not replace it.
		CHECK_THROWS(uniforms["noSuchMember"] = 2.5f);
	}

	SECTION("a resource handle")
	{
		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSComputeBufferTest"))
				.SetDebugName("CSComputeBufferTest"));

		auto handle          = bgl::BufferHandle();
		handle.bindlessIndex = 7u;

		kernel["gUniforms"]["outBuffer"].SetIfValid(handle);
		CHECK(static_cast<glm::uvec2>(kernel["gUniforms"]["outBuffer"]).x == 7u);

		CHECK_NOTHROW(kernel["gUniforms"]["noSuchBuffer"].SetIfValid(handle));
		CHECK_THROWS(kernel["gUniforms"]["noSuchBuffer"] = handle);
	}

	SECTION("a raw arena writes both of its descriptors from one assignment")
	{
		// The pair exists so the two cannot be handed different buffers (ADR-9). What makes that
		// hold is that one write lands in both halves -- if it reached only the raw one, every
		// material would sample whatever descriptor the view member was left holding.
		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSRawArenaBinding"))
				.SetDebugName("CSRawArenaBinding"));

		auto binding                  = bgl::RawArenaBinding();
		binding.buffer.bindlessIndex  = 11u;
		binding.handles.bindlessIndex = 12u;

		kernel["gUniforms"]["arena"] = binding;

		// Read at the leaves: the two halves are structs, and only the handle inside each is a
		// value the mirror can hand back.
		CHECK(static_cast<glm::uvec2>(kernel["gUniforms"]["arena"]["raw"]["rawBuffer"]).x == 11u);
		CHECK(
			static_cast<glm::uvec2>(kernel["gUniforms"]["arena"]["handles"]["handleBuffer"]).x ==
			12u);
	}
}

TEST_CASE("Only a type the mirror can store is assignable to an accessor", "[uniforms]")
{
	STATIC_REQUIRE(bgl::UniformAssignable<float>);
	STATIC_REQUIRE(bgl::UniformAssignable<glm::mat4>);
	STATIC_REQUIRE(bgl::UniformAssignable<bgl::DescriptorHandle>);

	// The four handle types reach their own assignment operators rather than the value one.
	STATIC_REQUIRE(bgl::UniformAssignable<bgl::BufferHandle>);
	STATIC_REQUIRE(bgl::UniformAssignable<bgl::SrvHandle>);
	STATIC_REQUIRE(bgl::UniformAssignable<bgl::SamplerHandle>);
	STATIC_REQUIRE(bgl::UniformAssignable<bgl::TextureAssetHandle>);

	// A double is the near miss that matters: writing 1.0 where the cbuffer declares a float is a
	// compile error, not a silent kNone that would throw only once the pass ran.
	STATIC_REQUIRE_FALSE(bgl::UniformAssignable<double>);
	STATIC_REQUIRE_FALSE(bgl::UniformAssignable<int64_t>);
	STATIC_REQUIRE_FALSE(bgl::UniformAssignable<std::string>);
}
