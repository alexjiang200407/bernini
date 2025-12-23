#include "buffer/DynamicVertexBuffer.h"
#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Add element", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("pos", gfx::ElementType::kFloat3);
	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 1 };

	auto& ret = (dbuf[0].At("pos").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));

	SECTION("First Assignment")
	{
		CHECK(ret.x == 1.0f);
		CHECK(ret.y == 2.0f);
		CHECK(ret.z == 3.0f);
	}

	SECTION("Second Assignment")
	{
		dbuf[0]["pos"] = glm::vec3{ 3.0f, 2.0f, 1.0f };

		CHECK(ret.x == 3.0f);
		CHECK(ret.y == 2.0f);
		CHECK(ret.z == 1.0f);
	}

	CHECK(dbuf.GetCount() == 1);
	CHECK(dbuf.GetTotalSize() == 12);
}

TEST_CASE("Multiple element", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("a", gfx::ElementType::kFloat3)
		.AddElement("b", gfx::ElementType::kFloat2)
		.AddElement("c", gfx::ElementType::kFloat)
		.AddElement("d", gfx::ElementType::kFloat4x4);

	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 1 };
	SECTION("Float3")
	{
		auto& ret = dbuf[0].At("a").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f });

		CHECK(ret.x == 1.0f);
		CHECK(ret.y == 2.0f);
		CHECK(ret.z == 3.0f);
	}

	SECTION("Float2")
	{
		auto& ret = dbuf[0].At("b").Assign(glm::vec2{ 1.0f, 2.0f });
		CHECK(ret.x == 1.0f);
		CHECK(ret.y == 2.0f);
	}

	SECTION("Float")
	{
		auto& ret = dbuf[0].At("c").Assign(42.0f);
		CHECK(ret == 42.0f);
	}

	SECTION("Matrix Assignment")
	{
		auto  mat = glm::mat4(1.0f);  // Identity matrix
		auto& ret = dbuf[0].At("d").Assign(mat);
		CHECK(ret[0][0] == 1.0f);
		CHECK(ret[1][1] == 1.0f);
		CHECK(ret[2][2] == 1.0f);
		CHECK(ret[3][3] == 1.0f);
		CHECK(ret[0][1] == 0.0f);
	}

	CHECK(dbuf.GetTotalSize() == 88);
}

TEST_CASE("Attribute doesn't exist", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("pos", gfx::ElementType::kFloat3);
	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 1 };

	CHECK_NOTHROW(dbuf[0]["color"]);
	CHECK_FALSE(dbuf[0]["color"].Valid());
	CHECK_FALSE(dbuf[0]["color"]);
	CHECK_THROWS(dbuf[0].At("color"));
	CHECK_NOTHROW(dbuf[0]["nonexistent"]);
	CHECK_THROWS(dbuf[0].At("nonexistent"));

	SECTION("Non existent nullop")
	{
		auto old         = dbuf[0]["pos"];
		dbuf[0]["color"] = glm::vec3{ 1.0f, 1.0f, 1.0f };
		CHECK(dbuf[0]["pos"] == old);
	}
}

TEST_CASE("Index out of bounds", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("pos", gfx::ElementType::kFloat3);
	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 5 };

	CHECK_NOTHROW(dbuf[0].At("pos"));
	CHECK_NOTHROW(dbuf[4].At("pos"));

	CHECK_THROWS(dbuf[5].At("pos"));
	CHECK_THROWS(dbuf[100].At("pos"));
	CHECK_THROWS(dbuf[-1].At("pos"));
}

TEST_CASE("Read after write", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("data", gfx::ElementType::kFloat4);
	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 1 };

	auto writeVal   = glm::vec4{ 1.5f, 2.5f, 3.5f, 4.5f };
	dbuf[0]["data"] = writeVal;

	auto& readVal   = (dbuf[0].At("data").Assign(glm::vec4{}));
	dbuf[0]["data"] = writeVal;

	CHECK(readVal.x == 1.5f);
	CHECK(readVal.y == 2.5f);
	CHECK(readVal.z == 3.5f);
	CHECK(readVal.w == 4.5f);
}

TEST_CASE("equality", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("data", gfx::ElementType::kFloat4);
	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 1 };

	auto val = glm::vec4{ 1.5f, 2.5f, 3.5f, 4.5f };
	dbuf[0]["data"].Assign(val);

	CHECK(dbuf[0]["data"] == val);
	CHECK_FALSE(dbuf[0]["data"] != val);
	CHECK(dbuf[0]["data"] != 1.0f);
	CHECK(dbuf[0]["data"] != glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
}

TEST_CASE("Element size mismatch", "[dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto dbufDesc = gfx::DynamicBufferDesc{};
	dbufDesc.AddElement("pos", gfx::ElementType::kFloat3);
	dbufDesc.AddElement("color", gfx::ElementType::kFloat4);
	auto dbuf = gfx::DynamicVertexBuffer{ device, std::move(dbufDesc), 1 };

	CHECK_NOTHROW(dbuf[0].At("pos").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
	CHECK_NOTHROW(dbuf[0].At("color").Assign(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f }));

	CHECK_THROWS(dbuf[0].At("pos").Assign(glm::vec4{ 1.0f, 2.0f, 3.0f, 4.0f }));
	CHECK_THROWS(dbuf[0].At("pos").Assign(glm::vec2{ 1.0f, 2.0f }));
	CHECK_THROWS(dbuf[0].At("color").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
}
