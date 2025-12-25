#include "buffer/DynamicVertexBuffer.h"
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

	SECTION("Basic")
	{
		auto vertexShaderBytecode = core::file::readFileBytes("shaders/VS_cube.cso"sv);

		auto shader = device->createShader(
			nvrhi::ShaderDesc{}
				.setShaderType(nvrhi::ShaderType::Vertex)
				.setDebugName("TestVertexShader"),
			vertexShaderBytecode.data(),
			vertexShaderBytecode.size());

		auto shaderInput = gfx::getVertexAttributes(shader);
		REQUIRE(shaderInput.size() == 1u);

		CHECK(shaderInput[0].type == gfx::VertexAttribute::kPosition);
		CHECK(shaderInput[0].format == nvrhi::Format::RGB32_FLOAT);
		CHECK(shaderInput[0].semanticName == "POSITION");
		CHECK(shaderInput[0].semanticIndex == 0);
	}

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

TEST_CASE("VertexLayoutGen", "[vertex_layout_gen][shader_reflection][dynamic_vertex_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };

	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dvbDesc = gfx::DynamicBufferDesc{};

	dvbDesc.AddElement("NORMAL", gfx::ElementType::kFloat3)
		.AddElement("POSITION", gfx::ElementType::kFloat3)
		.AddElement("POSITION1", gfx::ElementType::kFloat3)
		.AddElement("TANGENT", gfx::ElementType::kFloat4)
		.SetName("Test");

	auto dvb = gfx::DynamicVertexBuffer{ device, std::move(dvbDesc), 10 };

	for (int i = 0; i < 10; i++)
	{
		dvb[i]["POSITION"]  = glm::vec3{ i, i, i };
		dvb[i]["POSITION1"] = glm::vec3{ i, i, i };
		dvb[i]["NORMAL"]    = glm::vec3{ i, i, i };
		dvb[i]["TANGENT"]   = glm::vec4{ i, i, i, i };
	}

	auto vertexShaderBytecode = core::file::readFileBytes("shaders/VS_cube.cso"sv);

	auto shader = device->createShader(
		nvrhi::ShaderDesc{}
			.setShaderType(nvrhi::ShaderType::Vertex)
			.setDebugName("TestVertexShader"),
		vertexShaderBytecode.data(),
		vertexShaderBytecode.size());

	auto inputLayout = dvb.GenerateVertexLayout(device, shader);
	CHECK(inputLayout->getNumAttributes() == 1);

	auto attrDesc = inputLayout->getAttributeDesc(0);

	CHECK(attrDesc->offset == sizeof(glm::vec3));
	CHECK(attrDesc->format == nvrhi::Format::RGB32_FLOAT);
	CHECK(attrDesc->elementStride == sizeof(float) * 13);
}

TEST_CASE("Constant Buffer Reflection", "[constant_buffer][shader_reflection]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto pixelShaderBytes = core::file::readFileBytes("shaders/PS_Cbuf_test.cso"sv);
	auto x                = gfx::getDynamicConstantBufferDesc(pixelShaderBytes, 0);

	CHECK(1 == 1);
}
