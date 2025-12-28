#include "buffer/DynamicConstantBuffer.h"
#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

namespace
{
	struct ByteBuffer
	{
		std::vector<std::byte> data;

		ByteBuffer(size_t size) : data(size, std::byte{ 0 }) {}

		template <typename T>
		ByteBuffer&
		Set(const T& value, size_t offset)
		{
			static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

			if (offset + sizeof(T) > data.size())
				data.resize(offset + sizeof(T), std::byte{ 0 });

			std::memcpy(data.data() + offset, &value, sizeof(T));

			return *this;
		}

		std::byte*
		GetData()
		{
			return data.data();
		}
		size_t
		Size() const
		{
			return data.size();
		}
	};
}

TEST_CASE("Constant buffer alignment", "[dynamic_constant_buffer][dynamic_buffer][alignment]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Single float element")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat).SetName("PerFrameConstantBuffer");

		auto     cb        = gfx::DynamicConstantBuffer{ device, desc };
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);
		REQUIRE_NOTHROW(cb.At("a").Assign(1.0f));

		auto raw      = std::span{ cb.GetRawData(), totalSize };
		auto expected = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("float + float2 element")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat)
			.AddElement("b", gfx::ElementType::kFloat2)
			.SetName("PerFrameConstantBuffer");

		auto     cb        = gfx::DynamicConstantBuffer{ device, desc };
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);
		REQUIRE_NOTHROW(cb.At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("b").Assign(glm::vec2{ 2.0f, 3.0f }));

		auto raw = std::span{ cb.GetRawData(), totalSize };

		auto expected = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0).Set(glm::vec2{ 2.0f, 3.0f }, 4);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("struct + float")
	{
		struct MyStruct
		{
			glm::vec3 pos;
			float     intensity;
		};

		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.pos", gfx::ElementType::kFloat3)
			.AddElement("s.intensity", gfx::ElementType::kFloat)
			.AddElement("f", gfx::ElementType::kFloat)
			.SetName("PerFrameConstantBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("pos").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("s").At("intensity").Assign(4.0f));
		REQUIRE_NOTHROW(cb.At("f").Assign(5.0f));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		MyStruct s{ { 1.0f, 2.0f, 3.0f }, 4.0f };
		float    f = 5.0f;

		auto expected = ByteBuffer{ totalSize };
		expected.Set(s, 0).Set(5.0f, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("struct 16 offset")
	{
		struct MyStruct
		{
			glm::vec3 pos;
		};

		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("f", gfx::ElementType::kFloat)
			.AddStruct("s")
			.AddElement("s.pos", gfx::ElementType::kFloat3)
			.SetName("PerFrameConstantBuffer");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		REQUIRE_NOTHROW(cb.At("s").At("pos").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("f").Assign(5.0f));

		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		MyStruct s{ { 1.0f, 2.0f, 3.0f } };

		auto expected = ByteBuffer{ totalSize };
		expected.Set(5.0f, 0).Set(s, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Fit inside struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat)
			.AddElement("s.b", gfx::ElementType::kFloat)
			.AddElement("s.c", gfx::ElementType::kFloat)
			.AddElement("s.d", gfx::ElementType::kFloat)
			.SetName("PerFrameConstantBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("s").At("c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("s").At("d").Assign(4.0f));
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		struct
		{
			float a;
			float b;
			float c;
			float d;
		} s{ 1.0f, 2.0f, 3.0f, 4.0f };

		auto expected = ByteBuffer{ totalSize };
		expected.Set(s, 0);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Fields bigger than struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat2)
			.AddElement("s.b", gfx::ElementType::kFloat)
			.AddElement("s.c", gfx::ElementType::kFloat)
			.AddElement("s.d", gfx::ElementType::kFloat)
			.SetName("PerFrameConstantBuffer");

		auto     cb        = gfx::DynamicConstantBuffer{ device, desc };
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);
		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(glm::vec2{ 1.0f, 5.0f }));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("s").At("c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("s").At("d").Assign(4.0f));

		struct
		{
			float a[2];
			float b;
			float c;
			float d;
		} s{ { 1.0f, 5.0f }, 2.0f, 3.0f, 4.0f };

		auto expected = ByteBuffer{ totalSize };
		expected.Set(s, 0);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Empty struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("a", gfx::ElementType::kFloat)
			.AddElement("b", gfx::ElementType::kFloat)
			.AddElement("c", gfx::ElementType::kFloat)
			.AddElement("d", gfx::ElementType::kFloat)
			.SetName("PerFrameConstantBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("d").Assign(4.0f));
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		struct
		{
			float a;
			float b;
			float c;
			float d;
		} s{ 1.0f, 2.0f, 3.0f, 4.0f };

		auto expected = ByteBuffer{ totalSize };
		expected.Set(s, 0);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Small 1")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kShort)
			.AddElement("b", gfx::ElementType::kUShort)
			.AddElement("c", gfx::ElementType::kBool)
			.SetName("PerFrameConstantBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(static_cast<short>(1)));
		REQUIRE_NOTHROW(cb.At("b").Assign(static_cast<unsigned short>(2)));
		REQUIRE_NOTHROW(cb.At("c").Assign(TRUE));
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(static_cast<short>(1), 0).Set(static_cast<unsigned short>(2), 2).Set(1u, 4);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Small 2")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kShort)
			.AddElement("c", gfx::ElementType::kBool)
			.AddElement("b", gfx::ElementType::kUShort)
			.SetName("PerFrameConstantBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(static_cast<int16_t>(1)));
		REQUIRE_NOTHROW(cb.At("c").Assign(TRUE));
		REQUIRE_NOTHROW(cb.At("b").Assign(static_cast<uint16_t>(1)));

		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(static_cast<int16_t>(1), 0).Set(1u, 4).Set(static_cast<uint16_t>(1), 8);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Before and After struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("before", gfx::ElementType::kFloat2)
			.AddStruct("inner")
			.AddElement("inner.a", gfx::ElementType::kFloat)
			.AddElement("inner.b", gfx::ElementType::kFloat3)
			.AddElement("inner.c", gfx::ElementType::kFloat)
			.AddElement("after", gfx::ElementType::kFloat3)
			.SetName("PerFrameConstantBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("before").Assign(glm::vec2{ 1.0f, 2.0f }));
		REQUIRE_NOTHROW(cb.At("inner").At("a").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("inner").At("b").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("inner").At("c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("after").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		uint32_t totalSize = cb.GetTotalSize();
		CHECK(totalSize == 48);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec2{ 1.0f, 2.0f }, 0)
			.Set(3.0f, 16)
			.Set(glm::vec3{ 1.0f, 2.0f, 3.0f }, 20)
			.Set(3.0f, 32)
			.Set(glm::vec3{ 1.0f, 2.0f, 3.0f }, 36);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}
}

TEST_CASE("Constant buffer struct", "[dynamic_constant_buffer][dynamic_buffer][struct]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Correct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("Transform")
			.AddElement("Transform.position", gfx::ElementType::kFloat3)
			.AddElement("Transform.scale", gfx::ElementType::kFloat)
			.AddElement("id", gfx::ElementType::kFloat)
			.SetName("PerObject");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_NOTHROW(cb.At("Transform").At("position").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("Transform").At("scale").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("id").Assign(42.0f));

		auto totalSize = cb.GetTotalSize();

		REQUIRE(totalSize == 32);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec3{ 1.0f, 2.0f, 3.0f }, 0).Set(2.0f, 12).Set(42.0f, 16);

		auto* raw = cb.GetRawData();

		CHECK(std::memcmp(raw, expected.GetData(), totalSize) == 0);
	}

	SECTION("Indexing into non-existent struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("Transform")
			.AddElement("Transform.position", gfx::ElementType::kFloat3)
			.AddElement("id", gfx::ElementType::kFloat)
			.SetName("PerObject");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_THROWS_AS(cb.At("Transform").At("scale"), core::except::BerniniException);
		REQUIRE_THROWS_AS(cb.At("NonExistent").At("value"), core::except::BerniniException);

		REQUIRE_NOTHROW(cb["Transform"]["scale"]);
		REQUIRE_NOTHROW(cb["NonExistent"]["value"]);
	}

	SECTION("Element or struct names containing dot should throw")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};

		REQUIRE_THROWS_AS(
			desc.AddElement("id.dot", gfx::ElementType::kFloat),
			core::except::BerniniException);

		REQUIRE_THROWS_AS(desc.AddStruct("My.Struct"), core::except::BerniniException);
	}

	SECTION("At cannot contain .")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};

		desc.AddStruct("id").AddElement("id.dot", gfx::ElementType::kFloat);
		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_THROWS_AS(cb.At("id.dot"), core::except::BerniniException);
	}

	SECTION("No name should throw")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};

		REQUIRE_THROWS_AS(
			desc.AddElement("", gfx::ElementType::kFloat),
			core::except::BerniniException);

		REQUIRE_THROWS_AS(desc.AddStruct("My.Struct"), core::except::BerniniException);
	}

	SECTION("Add struct twice")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("Transform").AddElement("Transform.position", gfx::ElementType::kFloat3);

		REQUIRE_THROWS_AS(
			desc.AddStruct("Transform").AddElement("Transform.scale", gfx::ElementType::kFloat),
			core::except::BerniniException);
	}
}

TEST_CASE("Layout tests", "[dynamic_constant_buffer][dynamic_buffer][layout_tests]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Single element")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat);

		auto cb    = gfx::DynamicConstantBuffer{ device, desc };
		auto entry = cb.GetLayoutEntry("a");

		CHECK(entry.relativeOffset == 0);
		CHECK(entry.count == 1);
	}

	SECTION("Multiple element")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat);
		desc.AddElement("b", gfx::ElementType::kShort);
		desc.AddElement("c", gfx::ElementType::kShort);
		desc.AddElement("d", gfx::ElementType::kShort);
		desc.AddElement("e", gfx::ElementType::kFloat);
		desc.AddElement("f", gfx::ElementType::kFloat3);
		desc.AddElement("g", gfx::ElementType::kFloat3);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		{
			auto entry = cb.GetLayoutEntry("a");
			CHECK(entry.relativeOffset == 0);
			CHECK(entry.elemSize == 4);
		}

		{
			auto entry = cb.GetLayoutEntry("b");
			CHECK(entry.relativeOffset == 4);
			CHECK(entry.elemSize == 2);
		}

		{
			auto entry = cb.GetLayoutEntry("c");
			CHECK(entry.relativeOffset == 6);
			CHECK(entry.elemSize == 2);
		}

		{
			auto entry = cb.GetLayoutEntry("d");
			CHECK(entry.relativeOffset == 8);
			CHECK(entry.elemSize == 2);
		}

		{
			auto entry = cb.GetLayoutEntry("e");
			CHECK(entry.relativeOffset == 12);
			CHECK(entry.elemSize == 4);
		}

		{
			auto entry = cb.GetLayoutEntry("f");
			CHECK(entry.relativeOffset == 16);
			CHECK(entry.elemSize == 12);
		}

		{
			auto entry = cb.GetLayoutEntry("g");
			CHECK(entry.relativeOffset == 32);
			CHECK(entry.elemSize == 12);
		}

		CHECK(cb.GetTotalSize() == 48);
	}

	SECTION("Array")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat);
		desc.AddStruct("s");
		desc.AddElementArray("s.b", gfx::ElementType::kFloat, 4);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		CHECK(cb.GetTotalSize() == 80);

		{
			auto entry = cb.GetLayoutEntry("s.b");
			CHECK(entry.relativeOffset == 16);
			CHECK(entry.count == 4);
			CHECK(entry.elemSize == 4);
			CHECK(entry.stride == 16);
		}

		{
			auto entry = cb.GetLayoutEntry("s");
			CHECK(entry.relativeOffset == 16);
			CHECK(entry.count == 1);
			CHECK(entry.elemSize == 52);
			CHECK(entry.stride == 0);
		}
	}

	SECTION("Array of structs")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat)
			.AddStruct("s")
			.AddStructArray("s.a", 5)
			.AddElement("s.a.a", gfx::ElementType::kFloat)
			.AddStruct("s.a.b")
			.AddElement("s.a.b.a", gfx::ElementType::kShort)
			.AddElement("s.a.c", gfx::ElementType::kFloat3);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		CHECK(cb.GetTotalSize() == 176);
	}

	SECTION("Array of structs with internal arrays – stride & offsets")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStructArray("s", 3)
			.AddElement("s.a", gfx::ElementType::kFloat)
			.AddElementArray("s.b", gfx::ElementType::kFloat2, 2)
			.AddElement("s.c", gfx::ElementType::kFloat);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		// Struct layout expectations:
		// a : float          -> offset 0
		// b : float2[2]      -> offset 16, stride 16, total 32
		// c : float          -> offset 40
		// struct size        -> 48
		// array stride       -> 48

		auto structEntry = cb.GetLayoutEntry("s");
		CHECK(structEntry.stride == 48);
		CHECK(structEntry.count == 3);

		auto a = cb.GetLayoutEntry("s.a");
		CHECK(a.relativeOffset == 0);
		CHECK(a.elemSize == 4);

		auto b = cb.GetLayoutEntry("s.b");
		CHECK(b.relativeOffset == 16);
		CHECK(b.count == 2);
		CHECK(b.stride == 16);

		auto c = cb.GetLayoutEntry("s.c");
		CHECK(c.relativeOffset == 40);
		CHECK(c.elemSize == 4);

		CHECK(cb.GetTotalSize() == 3 * 48);
	}

	SECTION("Struct array stride with shorts + floats")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStructArray("s", 4)
			.AddElement("s.a", gfx::ElementType::kShort)
			.AddElement("s.b", gfx::ElementType::kFloat)
			.AddElement("s.c", gfx::ElementType::kShort);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		auto structEntry = cb.GetLayoutEntry("s");

		// a (2) + padding
		// b (4)
		// c (2) + padding
		// must round up to 16
		CHECK(structEntry.stride == 16);
		CHECK(cb.GetTotalSize() == 4 * 16);

		CHECK(cb.GetLayoutEntry("s.a").relativeOffset == 0);
		CHECK(cb.GetLayoutEntry("s.b").relativeOffset == 4);
		CHECK(cb.GetLayoutEntry("s.c").relativeOffset == 8);
	}

	//SECTION("Array with zero count should be rejected")
	//{
	//	auto desc = gfx::DynamicConstantBufferDesc{};

	//	REQUIRE_THROWS_AS(desc.AddStructArray("s", 0), core::except::BerniniException);
	//}
}

TEST_CASE("Constant buffer array", "[dynamic_constant_buffer][dynamic_buffer][array]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Single Element")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("a", gfx::ElementType::kFloat, 1);
		desc.AddElement("b", gfx::ElementType::kFloat);
		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		auto totalSize = cb.GetTotalSize();

		CHECK(totalSize == 16);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0).Set(2.0f, 16).Set(3.0f, 32).Set(4.0f, 48);
	}

	SECTION("Element Array")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("a", gfx::ElementType::kFloat, 4);
		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		auto a = cb.At("a");
		REQUIRE_NOTHROW(a.At(0).Assign(1.0f));
		REQUIRE_NOTHROW(a.At(1).Assign(2.0f));
		REQUIRE_NOTHROW(a.At(2).Assign(3.0f));
		REQUIRE_NOTHROW(a.At(3).Assign(4.0f));
		REQUIRE_THROWS_AS(a.At(4).Assign(1.0f), core::except::BerniniException);

		auto totalSize = cb.GetTotalSize();
		auto expected  = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0).Set(2.0f, 16).Set(3.0f, 32).Set(4.0f, 48);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);
		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Nested array")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("a", gfx::ElementType::kFloat, 4);
		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		auto a = cb.At("a");
		REQUIRE_NOTHROW(a.At(0).Assign(1.0f));
		REQUIRE_NOTHROW(a.At(1).Assign(2.0f));
		REQUIRE_NOTHROW(a.At(2).Assign(3.0f));
		REQUIRE_NOTHROW(a.At(3).Assign(4.0f));
		REQUIRE_THROWS_AS(a.At(4).Assign(1.0f), core::except::BerniniException);

		auto totalSize = cb.GetTotalSize();
		auto expected  = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0).Set(2.0f, 16).Set(3.0f, 32).Set(4.0f, 48);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);
		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Struct larger than 16 bytes (float3 + float2)")
	{
		struct BigStruct
		{
			glm::vec3 a;  // 12
			glm::vec2 b;  // 8  -> total 20, padded to 32
		};

		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat3)
			.AddElement("s.b", gfx::ElementType::kFloat2)
			.SetName("PerFrame");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(glm::vec3{ 1, 2, 3 }));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec2{ 4, 5 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec3{ 1, 2, 3 }, 0).Set(glm::vec2{ 4, 5 }, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}
}

TEST_CASE("Constant buffer alignment", "[dynamic_constant_buffer][dynamic_buffer][array]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Struct larger than 16 bytes (float3 + float2)")
	{
		struct BigStruct
		{
			glm::vec3 a;  // 12
			glm::vec2 b;  // 8  -> total 20, padded to 32
		};

		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat3)
			.AddElement("s.b", gfx::ElementType::kFloat2)
			.SetName("PerFrame");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(glm::vec3{ 1, 2, 3 }));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec2{ 4, 5 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec3{ 1, 2, 3 }, 0).Set(glm::vec2{ 4, 5 }, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Struct 28 bytes padded to 32")
	{
		struct BigStruct
		{
			glm::vec4 a;  // 16
			glm::vec3 b;  // 12 -> total 28
		};

		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat4)
			.AddElement("s.b", gfx::ElementType::kFloat3)
			.SetName("PerFrame");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(glm::vec4{ 1, 2, 3, 4 }));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec3{ 5, 6, 7 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec4{ 1, 2, 3, 4 }, 0).Set(glm::vec3{ 5, 6, 7 }, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Struct larger than 32 bytes spills to multiple registers")
	{
		struct HugeStruct
		{
			glm::vec4 a;  // 16
			glm::vec4 b;  // 16
			glm::vec2 c;  // 8  -> total 40, padded to 48
		};

		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat4)
			.AddElement("s.b", gfx::ElementType::kFloat4)
			.AddElement("s.c", gfx::ElementType::kFloat2)
			.SetName("PerFrame");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(glm::vec4{ 1, 2, 3, 4 }));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec4{ 5, 6, 7, 8 }));
		REQUIRE_NOTHROW(cb.At("s").At("c").Assign(glm::vec2{ 9, 10 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 48);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec4{ 1, 2, 3, 4 }, 0)
			.Set(glm::vec4{ 5, 6, 7, 8 }, 16)
			.Set(glm::vec2{ 9, 10 }, 32);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Large struct followed by element aligns correctly")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat3)
			.AddElement("s.b", gfx::ElementType::kFloat2)  // struct = 20 → 32
			.AddElement("after", gfx::ElementType::kFloat);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(glm::vec3{ 1, 2, 3 }));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec2{ 4, 5 }));
		REQUIRE_NOTHROW(cb.At("after").Assign(6.0f));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec3{ 1, 2, 3 }, 0).Set(glm::vec2{ 4, 5 }, 16).Set(6.0f, 24);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Float array followed by scalar aligns to next register")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("x", gfx::ElementType::kFloat, 2)
			.AddElement("b", gfx::ElementType::kFloat)
			.SetName("PerFrame");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_NOTHROW(cb.At("x").At(0).Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("x").At(1).Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("b").Assign(3.0f));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected
			.Set(1.0f, 0)    // x[0]
			.Set(2.0f, 16)   // x[1]
			.Set(3.0f, 20);  // b starts in next register

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}
}

TEST_CASE("Move semantics tests", "[dynamic_constant_buffer][dynamic_buffer][move_tests]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto desc = gfx::DynamicConstantBufferDesc{};
	desc.AddStruct("S")
		.AddElement("S.v", gfx::ElementType::kFloat4)
		.AddElementArray("S.arr", gfx::ElementType::kFloat, 2)
		.AddStructArray("S.nested", 1)
		.AddElement("S.nested.a", gfx::ElementType::kFloat3);

	SECTION("Move constructor preserves contents")
	{
		gfx::DynamicConstantBuffer src(device, desc);
		src.At("S").At("v").Assign(glm::vec4{ 1, 2, 3, 4 });

		gfx::DynamicConstantBuffer dst(std::move(src));

		REQUIRE_NOTHROW(dst.At("S").At("v"));
		REQUIRE_NOTHROW(dst.At("S").At("v").Assign(glm::vec4{ 4, 3, 2, 1 }));
	}

	SECTION("Move assignment overwrites existing state")
	{
		gfx::DynamicConstantBuffer a(device, desc);
		gfx::DynamicConstantBuffer b(device, desc);

		a.At("S").At("v").Assign(glm::vec4{ 1, 1, 1, 1 });
		b.At("S").At("v").Assign(glm::vec4{ 9, 9, 9, 9 });

		b = std::move(a);

		REQUIRE_NOTHROW(b.At("S").At("v"));
		REQUIRE_NOTHROW(b.At("S").At("v").Assign(glm::vec4{ 2, 2, 2, 2 }));
	}

	SECTION("Moved-from buffer can be reused")
	{
		gfx::DynamicConstantBuffer a(device, desc);
		gfx::DynamicConstantBuffer b(std::move(a));

		a = gfx::DynamicConstantBuffer(device, desc);

		REQUIRE_NOTHROW(a.At("S").At("v"));
		REQUIRE_NOTHROW(a.At("S").At("v").Assign(glm::vec4{ 7, 7, 7, 7 }));
	}

	SECTION("Self move assignment is safe")
	{
		gfx::DynamicConstantBuffer buf(device, desc);
		buf.At("S").At("v").Assign(glm::vec4{ 1, 2, 3, 4 });

		buf = std::move(buf);

		REQUIRE_NOTHROW(buf.At("S").At("v"));
		REQUIRE_NOTHROW(buf.At("S").At("v").Assign(glm::vec4{ 4, 3, 2, 1 }));
	}

	SECTION("View obtained before move does not remain valid")
	{
		gfx::DynamicConstantBuffer a(device, desc);
		auto                       view = a.At("S").At("v");

		gfx::DynamicConstantBuffer b(std::move(a));

		REQUIRE_THROWS(view.Assign(glm::vec4{ 1, 1, 1, 1 }));
	}

	SECTION("Nested layout survives move")
	{
		gfx::DynamicConstantBuffer a(device, desc);
		a.At("S").At("v").Assign(glm::vec4{ 1, 2, 3, 4 });

		gfx::DynamicConstantBuffer b(std::move(a));

		REQUIRE_NOTHROW(b.At("S").At("v"));
		REQUIRE_NOTHROW(b.At("S").At("v").Assign(glm::vec4{ 4, 3, 2, 1 }));
	}

	SECTION("Move empty buffer")
	{
		gfx::DynamicConstantBuffer empty;
		gfx::DynamicConstantBuffer moved(std::move(empty));

		REQUIRE(moved.GetTotalSize() == 0);
		REQUIRE_THROWS_AS(moved.At("S").At("v"), core::except::BerniniException);
	}

	SECTION("Move large buffer preserves all data")
	{
		gfx::DynamicConstantBuffer buf(device, desc);
		buf.At("S").At("v").Assign(glm::vec4{ 1, 2, 3, 4 });
		buf.At("S").At("arr").At(0).Assign(10.0f);
		buf.At("S").At("arr").At(1).Assign(20.0f);
		buf.At("S").At("nested").At(0).At("a").Assign(glm::vec3{ 5, 6, 7 });

		gfx::DynamicConstantBuffer moved(std::move(buf));

		CHECK_NOTHROW(moved.At("S").At("v").Assign(glm::vec4{ 4, 3, 2, 1 }));
		CHECK_NOTHROW(moved.At("S").At("arr").At(1).Assign(30.0f));
		CHECK_NOTHROW(moved.At("S").At("nested").At(0).At("a").Assign(glm::vec3{ 7, 8, 9 }));
	}

	SECTION("Move assignment overwrites existing data")
	{
		gfx::DynamicConstantBuffer a(device, desc);
		gfx::DynamicConstantBuffer b(device, desc);

		a.At("S").At("v").Assign(glm::vec4{ 1, 1, 1, 1 });
		b.At("S").At("v").Assign(glm::vec4{ 9, 9, 9, 9 });

		b = std::move(a);

		CHECK_NOTHROW(b.At("S").At("v").Assign(glm::vec4{ 2, 2, 2, 2 }));
	}

	SECTION("Nested arrays survive move")
	{
		gfx::DynamicConstantBuffer buf(device, desc);
		buf.At("S").At("arr").At(0).Assign(1.0f);
		buf.At("S").At("arr").At(1).Assign(2.0f);

		gfx::DynamicConstantBuffer moved(std::move(buf));

		CHECK_NOTHROW(moved.At("S").At("arr").At(0).Assign(10.0f));
		CHECK_NOTHROW(moved.At("S").At("arr").At(1).Assign(20.0f));
	}

	SECTION("View obtained before move becomes invalid")
	{
		gfx::DynamicConstantBuffer buf(device, desc);
		auto                       view = buf.At("S").At("v");

		gfx::DynamicConstantBuffer moved(std::move(buf));

		REQUIRE_THROWS(view.Assign(glm::vec4{ 1, 1, 1, 1 }));
	}

	SECTION("Self move assignment is safe")
	{
		gfx::DynamicConstantBuffer buf(device, desc);
		buf.At("S").At("v").Assign(glm::vec4{ 1, 2, 3, 4 });

		buf = std::move(buf);

		CHECK_NOTHROW(buf.At("S").At("v").Assign(glm::vec4{ 4, 3, 2, 1 }));
	}

	SECTION("Move after partial initialization")
	{
		gfx::DynamicConstantBuffer buf(device, desc);
		buf.At("S").At("v").Assign(glm::vec4{ 1, 1, 1, 1 });

		gfx::DynamicConstantBuffer moved(std::move(buf));

		REQUIRE_FALSE(moved.At("S").At("v").IsNull());

		REQUIRE_NOTHROW(moved.At("S").At("arr").At(0).Assign(3.0f));
	}

	SECTION("Move of buffer with only arrays")
	{
		gfx::DynamicConstantBufferDesc arrayDesc;
		arrayDesc.AddElementArray("arr", gfx::ElementType::kFloat, 8);

		gfx::DynamicConstantBuffer buf(device, arrayDesc);
		buf.At("arr").At(0).Assign(1.0f);
		buf.At("arr").At(7).Assign(8.0f);

		gfx::DynamicConstantBuffer moved(std::move(buf));

		CHECK_NOTHROW(moved.At("arr").At(0).Assign(10.0f));
		CHECK_NOTHROW(moved.At("arr").At(7).Assign(80.0f));
	}
}

TEST_CASE(
	"Edge cases - integer and matrix types",
	"[dynamic_constant_buffer][edge_cases][integer_matrix]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("All integer types mix")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kInt)
			.AddElement("b", gfx::ElementType::kUInt)
			.AddElement("c", gfx::ElementType::kShort)
			.AddElement("d", gfx::ElementType::kUShort)
			.SetName("IntegerBuffer");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(static_cast<int32_t>(-42)));
		REQUIRE_NOTHROW(cb.At("b").Assign(static_cast<uint32_t>(100)));
		REQUIRE_NOTHROW(cb.At("c").Assign(static_cast<int16_t>(-10)));
		REQUIRE_NOTHROW(cb.At("d").Assign(static_cast<uint16_t>(20)));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(static_cast<int32_t>(-42), 0)
			.Set(static_cast<uint32_t>(100), 4)
			.Set(static_cast<int16_t>(-10), 8)
			.Set(static_cast<uint16_t>(20), 10);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Float4x4 matrix single element")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("transform", gfx::ElementType::kFloat4x4).SetName("MatrixBuffer");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 64);

		auto matrix = glm::mat4{ 1.0f };
		REQUIRE_NOTHROW(cb.At("transform").Assign(matrix));

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(matrix, 0);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Float4x4 matrix with preceding float")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("scale", gfx::ElementType::kFloat)
			.AddElement("transform", gfx::ElementType::kFloat4x4)
			.SetName("MatrixBuffer");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		// scale at 0, matrix starts at 16
		CHECK(totalSize == 80);

		REQUIRE_NOTHROW(cb.At("scale").Assign(2.0f));
		auto matrix = glm::mat4{ 1.0f };
		REQUIRE_NOTHROW(cb.At("transform").Assign(matrix));

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(2.0f, 0).Set(matrix, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Multiple matrix elements in struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("matrices")
			.AddElement("matrices.view", gfx::ElementType::kFloat4x4)
			.AddElement("matrices.projection", gfx::ElementType::kFloat4x4)
			.SetName("MatricesBuffer");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 128);

		auto view = glm::mat4{ 1.0f };
		auto proj = glm::mat4{ 2.0f };
		REQUIRE_NOTHROW(cb.At("matrices").At("view").Assign(view));
		REQUIRE_NOTHROW(cb.At("matrices").At("projection").Assign(proj));

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(view, 0).Set(proj, 64);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}
}

TEST_CASE("Edge cases - boundary conditions", "[dynamic_constant_buffer][edge_cases][boundaries]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Multiple structs with staggered sizes")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s1")
			.AddElement("s1.a", gfx::ElementType::kFloat)
			.AddStruct("s2")
			.AddElement("s2.b", gfx::ElementType::kFloat2)
			.AddStruct("s3")
			.AddElement("s3.c", gfx::ElementType::kFloat3)
			.SetName("StaggeredStructs");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s1").At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("s2").At("b").Assign(glm::vec2{ 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("s3").At("c").Assign(glm::vec3{ 4.0f, 5.0f, 6.0f }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 48);
	}

	SECTION("Deeply nested structures")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("outer")
			.AddElement("outer.a", gfx::ElementType::kFloat)
			.AddStruct("outer.middle")
			.AddElement("outer.middle.b", gfx::ElementType::kFloat2)
			.AddElement("outer.middle.c", gfx::ElementType::kFloat3)
			.SetName("DeeplyNested");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("outer").At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("outer").At("middle").At("b").Assign(glm::vec2{ 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("outer").At("middle").At("c").Assign(glm::vec3{ 4.0f, 5.0f, 6.0f }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 48);
	}

	SECTION("Single byte followed by large struct alignment")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("single", gfx::ElementType::kBool)
			.AddStruct("s")
			.AddElement("s.vec4", gfx::ElementType::kFloat4)
			.AddElement("s.vec4_2", gfx::ElementType::kFloat4)
			.SetName("ByteBeforeLargeStruct");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("single").Assign(TRUE));
		REQUIRE_NOTHROW(cb.At("s").At("vec4").Assign(glm::vec4{ 1, 2, 3, 4 }));
		REQUIRE_NOTHROW(cb.At("s").At("vec4_2").Assign(glm::vec4{ 5, 6, 7, 8 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 48);
	}

	SECTION("Maximum packing in 16-byte register")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("f", gfx::ElementType::kFloat)
			.AddElement("s1", gfx::ElementType::kShort)
			.AddElement("s2", gfx::ElementType::kShort)
			.AddElement("s3", gfx::ElementType::kShort)
			.AddElement("s4", gfx::ElementType::kShort)
			.SetName("MaxPackedRegister");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("f").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("s1").Assign(static_cast<int16_t>(1)));
		REQUIRE_NOTHROW(cb.At("s2").Assign(static_cast<int16_t>(2)));
		REQUIRE_NOTHROW(cb.At("s3").Assign(static_cast<int16_t>(3)));
		REQUIRE_NOTHROW(cb.At("s4").Assign(static_cast<int16_t>(4)));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);
	}

	SECTION("Array of struct containing array")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStructArray("items", 3)
			.AddElement("items.id", gfx::ElementType::kFloat)
			.AddElementArray("items.data", gfx::ElementType::kFloat, 2)
			.SetName("NestedArray");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("items").At(0).At("id").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("items").At(1).At("data").At(0).Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("items").At(2).At("id").Assign(3.0f));

		auto totalSize = cb.GetTotalSize();
		// Each item: float (4) + array[2] of float (2*16), item size aligns to 48
		// 3 items * 48 = 144
		CHECK(totalSize == 144);
	}
}

TEST_CASE("Edge cases - boundary crossing", "[dynamic_constant_buffer][edge_cases][crossing]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Float3 at end of register crosses boundary")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kFloat)
			.AddElement("b", gfx::ElementType::kFloat)
			.AddElement("c", gfx::ElementType::kFloat)
			.AddElement("d", gfx::ElementType::kFloat3)
			.SetName("CrossesBoundary");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("d").Assign(glm::vec3{ 4, 5, 6 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 32);

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0).Set(2.0f, 4).Set(3.0f, 8).Set(glm::vec3{ 4, 5, 6 }, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Float2 at unaligned offset wraps correctly")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("a", gfx::ElementType::kShort)
			.AddElement("b", gfx::ElementType::kShort)
			.AddElement("c", gfx::ElementType::kFloat2)
			.SetName("UnalignedWrap");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(static_cast<int16_t>(1)));
		REQUIRE_NOTHROW(cb.At("b").Assign(static_cast<int16_t>(2)));
		REQUIRE_NOTHROW(cb.At("c").Assign(glm::vec2{ 3.0f, 4.0f }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(static_cast<int16_t>(1), 0)
			.Set(static_cast<int16_t>(2), 2)
			.Set(glm::vec2{ 3.0f, 4.0f }, 4);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Array elements each aligned to 16-byte boundaries")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("values", gfx::ElementType::kFloat3, 3).SetName("AlignedArray");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("values").At(0).Assign(glm::vec3{ 1, 2, 3 }));
		REQUIRE_NOTHROW(cb.At("values").At(1).Assign(glm::vec3{ 4, 5, 6 }));
		REQUIRE_NOTHROW(cb.At("values").At(2).Assign(glm::vec3{ 7, 8, 9 }));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 48);

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec3{ 1, 2, 3 }, 0)
			.Set(glm::vec3{ 4, 5, 6 }, 16)
			.Set(glm::vec3{ 7, 8, 9 }, 32);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}
}

TEST_CASE("Edge cases - zero and extreme values", "[dynamic_constant_buffer][edge_cases][values]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("All zero values")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("f", gfx::ElementType::kFloat)
			.AddElement("i", gfx::ElementType::kInt)
			.AddElement("v3", gfx::ElementType::kFloat3)
			.SetName("ZeroValues");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("f").Assign(0.0f));
		REQUIRE_NOTHROW(cb.At("i").Assign(0));
		REQUIRE_NOTHROW(cb.At("v3").Assign(glm::vec3{ 0 }));

		auto totalSize = cb.GetTotalSize();
		auto raw       = std::span<std::byte>(cb.GetRawData(), totalSize);

		for (size_t i = 0; i < totalSize; ++i)
		{
			CHECK(raw[i] == std::byte{ 0 });
		}
	}

	SECTION("Extreme float values")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("tiny", gfx::ElementType::kFloat)
			.AddElement("huge", gfx::ElementType::kFloat)
			.AddElement("negative", gfx::ElementType::kFloat)
			.SetName("ExtremeValues");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("tiny").Assign(std::numeric_limits<float>::min()));
		REQUIRE_NOTHROW(cb.At("huge").Assign(std::numeric_limits<float>::max()));
		REQUIRE_NOTHROW(cb.At("negative").Assign(-1e6f));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);

		auto raw      = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected = ByteBuffer{ totalSize };
		expected.Set(std::numeric_limits<float>::min(), 0)
			.Set(std::numeric_limits<float>::max(), 4)
			.Set(-1e6f, 8);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Integer boundary values")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("u_max", gfx::ElementType::kUInt)
			.AddElement("i_min", gfx::ElementType::kInt)
			.AddElement("i_max", gfx::ElementType::kInt)
			.SetName("IntBoundaries");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("u_max").Assign(std::numeric_limits<uint32_t>::max()));
		REQUIRE_NOTHROW(cb.At("i_min").Assign(std::numeric_limits<int32_t>::min()));
		REQUIRE_NOTHROW(cb.At("i_max").Assign(std::numeric_limits<int32_t>::max()));

		auto totalSize = cb.GetTotalSize();
		auto raw       = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected  = ByteBuffer{ totalSize };
		expected.Set(std::numeric_limits<uint32_t>::max(), 0)
			.Set(std::numeric_limits<int32_t>::min(), 4)
			.Set(std::numeric_limits<int32_t>::max(), 8);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Negative integer values")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("s", gfx::ElementType::kShort)
			.AddElement("i", gfx::ElementType::kInt)
			.SetName("NegativeValues");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s").Assign(static_cast<int16_t>(-32768)));
		REQUIRE_NOTHROW(cb.At("i").Assign(static_cast<int32_t>(-2147483648)));

		auto totalSize = cb.GetTotalSize();
		CHECK(totalSize == 16);
	}
}

TEST_CASE("Edge cases - large arrays", "[dynamic_constant_buffer][edge_cases][large_arrays]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Large element array")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("data", gfx::ElementType::kFloat, 64).SetName("LargeArray");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		// 64 floats, each 16-byte aligned: first at 0, rest at 16*i
		CHECK(totalSize == 1024);

		for (uint32_t i = 0; i < 64; ++i)
		{
			REQUIRE_NOTHROW(cb.At("data").At(i).Assign(static_cast<float>(i)));
		}
	}

	SECTION("Large struct array")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStructArray("items", 16)
			.AddElement("items.value", gfx::ElementType::kFloat)
			.SetName("LargeStructArray");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		// 16 structs, each 16 bytes
		CHECK(totalSize == 256);

		for (uint32_t i = 0; i < 16; ++i)
		{
			REQUIRE_NOTHROW(cb.At("items").At(i).At("value").Assign(static_cast<float>(i)));
		}
	}

	SECTION("Array of vectors")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("vectors", gfx::ElementType::kFloat4, 8).SetName("VectorArray");

		auto cb        = gfx::DynamicConstantBuffer{ device, desc };
		auto totalSize = cb.GetTotalSize();
		// 8 float4s, each 16-byte aligned
		CHECK(totalSize == 128);

		for (uint32_t i = 0; i < 8; ++i)
		{
			REQUIRE_NOTHROW(cb.At("vectors").At(i).Assign(glm::vec4{ i, i + 1, i + 2, i + 3 }));
		}
	}

	SECTION("Array bounds checking")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("arr", gfx::ElementType::kFloat, 10).SetName("BoundsCheck");

		auto cb  = gfx::DynamicConstantBuffer{ device, desc };
		auto arr = cb.At("arr");

		REQUIRE_NOTHROW(arr.At(0).Assign(1.0f));
		REQUIRE_NOTHROW(arr.At(9).Assign(10.0f));
		REQUIRE_THROWS_AS(arr.At(10).Assign(11.0f), core::except::BerniniException);
	}
}

TEST_CASE("Edge cases - Naming", "[dynamic_constant_buffer][edge_cases][names]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Overlapping and prefix names should throw")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};

		desc.AddStruct("s");

		REQUIRE_THROWS_AS(
			desc.AddElement("s", gfx::ElementType::kFloat),
			core::except::BerniniException);

		REQUIRE_THROWS_AS(
			desc.AddElement("s.a", gfx::ElementType::kFloat).AddStruct("s.a"),
			core::except::BerniniException);
	}

	SECTION("Adding child before parent should throw")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};

		REQUIRE_THROWS_AS(
			desc.AddElement("s.a", gfx::ElementType::kFloat),
			core::except::BerniniException);
	}
}

//TEST_CASE("Edge cases - View", "[dynamic_constant_buffer][edge_cases][view]")
//{
//	auto gfxDesc     = GfxOptions{};
//	gfxDesc.headless = true;
//	gfxDesc.width    = 800;
//	gfxDesc.height   = 600;
//
//	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
//	REQUIRE(gfx);
//
//	auto device = gfx->GetDevice();
//
//	SECTION("View becomes invalid after parent destruction")
//	{
//		gfx::DynamicConstantBufferView view;
//
//		{
//			auto desc = gfx::DynamicConstantBufferDesc{};
//			desc.AddElement("a", gfx::ElementType::kFloat);
//			auto cb = gfx::DynamicConstantBuffer{ device, desc };
//			view    = cb.At("a");
//		}
//
//		REQUIRE_THROWS_AS(view.Assign(1.0f), core::except::BerniniException);
//	}
//
//	SECTION("View invalid after move-assignment")
//	{
//		auto desc = gfx::DynamicConstantBufferDesc{};
//		desc.AddElement("a", gfx::ElementType::kFloat);
//
//		gfx::DynamicConstantBuffer cb1{ device, desc };
//		gfx::DynamicConstantBuffer cb2{ device, desc };
//
//		auto view = cb1.At("a");
//
//		cb2 = std::move(cb1);
//
//		REQUIRE_THROWS_AS(view.Assign(1.0f), core::except::BerniniException);
//	}
//}

TEST_CASE(
	"Edge cases - assignment and reassignment",
	"[dynamic_constant_buffer][edge_cases][reassign]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Multiple reassignments to same field")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElement("value", gfx::ElementType::kFloat).SetName("Reassign");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_NOTHROW(cb.At("value").Assign(1.0f));
		auto* raw1 = cb.GetRawData();
		CHECK(*reinterpret_cast<float*>(raw1) == 1.0f);

		REQUIRE_NOTHROW(cb.At("value").Assign(2.0f));
		auto* raw2 = cb.GetRawData();
		CHECK(*reinterpret_cast<float*>(raw2) == 2.0f);

		REQUIRE_NOTHROW(cb.At("value").Assign(3.0f));
		auto* raw3 = cb.GetRawData();
		CHECK(*reinterpret_cast<float*>(raw3) == 3.0f);
	}

	SECTION("Reassignment in struct fields")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("s")
			.AddElement("s.a", gfx::ElementType::kFloat)
			.AddElement("s.b", gfx::ElementType::kFloat2)
			.SetName("StructReassign");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec2{ 2, 3 }));

		REQUIRE_NOTHROW(cb.At("s").At("a").Assign(10.0f));
		REQUIRE_NOTHROW(cb.At("s").At("b").Assign(glm::vec2{ 20, 30 }));

		auto totalSize = cb.GetTotalSize();
		auto raw       = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected  = ByteBuffer{ totalSize };
		expected.Set(10.0f, 0).Set(glm::vec2{ 20, 30 }, 4);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	SECTION("Array element reassignment")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("arr", gfx::ElementType::kFloat, 3).SetName("ArrayReassign");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_NOTHROW(cb.At("arr").At(0).Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("arr").At(1).Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("arr").At(2).Assign(3.0f));

		REQUIRE_NOTHROW(cb.At("arr").At(1).Assign(20.0f));

		auto totalSize = cb.GetTotalSize();
		auto raw       = std::span<std::byte>(cb.GetRawData(), totalSize);
		auto expected  = ByteBuffer{ totalSize };
		expected.Set(1.0f, 0).Set(2.0f, 16).Set(3.0f, 32);
		expected.Set(20.0f, 16);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}
}
