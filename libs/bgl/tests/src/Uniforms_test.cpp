#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/MeshletPipeline.h"

TEST_CASE("Uniforms", "[uniforms]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;
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

		pipelineDesc.SetMeshShader(device->CreateShader(
			"shaders/MSUniformReflectionScalar.dxil",
			"MSUniformReflectionScalar"));

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
			CHECK(uniforms["f2"].GetOffset() == 4u);  // Offset: f1 (0) + 4 bytes

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
			CHECK(uniforms["i2"].GetOffset() == 52u);

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
			CHECK(uniforms["u2"].GetOffset() == 100u);

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
