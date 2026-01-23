#include "buffer/DynamicConstantBuffer.h"
#include "graphics/Graphics.h"
#include "shader_reflect/ShaderInput.h"
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>

TEST_CASE("Vertex Attribute", "[vertex_attribute][shader_reflection]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };

	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Multiple")
	{
		auto vertexShaderBytecode = core::file::readFileBytes("shaders/VS_test0.cso"sv);

		auto shader = device->createShader(
			nvrhi::ShaderDesc{}
				.setShaderType(nvrhi::ShaderType::Vertex)
				.setDebugName("TestVertexShader"),
			vertexShaderBytecode.data(),
			vertexShaderBytecode.size());

		auto shaderInput = gfx::getVertexAttributes(shader);
		REQUIRE(shaderInput.size() == 5u);

		CHECK(shaderInput[0].type == gfx::VertexAttribute::kPosition);
		CHECK(shaderInput[0].format == nvrhi::Format::RGB32_FLOAT);
		CHECK(shaderInput[0].semanticName == "POSITION");
		CHECK(shaderInput[0].semanticId == "POSITION0");
		CHECK(shaderInput[0].semanticIndex == 0);

		CHECK(shaderInput[1].type == gfx::VertexAttribute::kPosition);
		CHECK(shaderInput[1].format == nvrhi::Format::RGB32_FLOAT);
		CHECK(shaderInput[1].semanticName == "POSITION");
		CHECK(shaderInput[1].semanticId == "POSITION1");
		CHECK(shaderInput[1].semanticIndex == 1);

		CHECK(shaderInput[2].type == gfx::VertexAttribute::kUV);
		CHECK(shaderInput[2].format == nvrhi::Format::RG32_FLOAT);
		CHECK(shaderInput[2].semanticName == "TEXCOORD");
		CHECK(shaderInput[2].semanticId == "TEXCOORD0");
		CHECK(shaderInput[2].semanticIndex == 0);

		CHECK(shaderInput[3].type == gfx::VertexAttribute::kNormal);
		CHECK(shaderInput[3].format == nvrhi::Format::RGB32_FLOAT);
		CHECK(shaderInput[3].semanticName == "NORMAL");
		CHECK(shaderInput[3].semanticId == "NORMAL0");
		CHECK(shaderInput[3].semanticIndex == 0);

		CHECK(shaderInput[4].type == gfx::VertexAttribute::kTangent);
		CHECK(shaderInput[4].format == nvrhi::Format::RGB32_FLOAT);
		CHECK(shaderInput[4].semanticName == "TANGENT");
		CHECK(shaderInput[4].semanticId == "TANGENT0");
		CHECK(shaderInput[4].semanticIndex == 0);
	}
}

TEST_CASE("Material constant buffer", "[constant_buffer][shader_reflection]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Simple test")
	{
		auto cb = gfx::DynamicConstantBuffer(gfx->GetDevice(), "shaders/PS_Cbuf_test.cso"sv, 3, 3);

		CHECK(cb["a"].IsValid());
		CHECK(cb["b"].IsValid());
		CHECK(cb["c"].IsValid());
		CHECK(cb["d"].IsValid());

		CHECK(cb["a"].Size() == 16);
		CHECK(cb["b"].Size() == 8);
		CHECK(cb["c"].Size() == 4);
		CHECK(cb["d"].Size() == 4);

		CHECK(cb.GetTotalSize() == 32);
	}
}
