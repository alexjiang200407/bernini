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

		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 16);
		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb["a"] = 1.0f);

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 16);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb["a"] = 1.0f);
		REQUIRE_NOTHROW(cb["b"] = glm::vec2{ 2.0f, 3.0f });

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
		auto desc1     = desc.ToDynamicBufferDesc();
		auto totalSize = desc1.GetTotalSize();

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb["s.pos"] = glm::vec3{ 1.0f, 2.0f, 3.0f });
		REQUIRE_NOTHROW(cb["s.intensity"] = 4.0f);
		REQUIRE_NOTHROW(cb["f"] = 5.0f);

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
		auto desc1     = desc.ToDynamicBufferDesc();
		auto totalSize = desc1.GetTotalSize();

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s.pos") = glm::vec3{ 1.0f, 2.0f, 3.0f });
		REQUIRE_NOTHROW(cb.At("f") = 5.0f);

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 16);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("s.a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("s.b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("s.c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("s.d").Assign(4.0f));

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 32);

		auto cb  = gfx::DynamicConstantBuffer{ device, desc };
		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		REQUIRE_NOTHROW(cb.At("s.a").Assign(glm::vec2{ 1.0f, 5.0f }));
		REQUIRE_NOTHROW(cb.At("s.b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("s.c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("s.d").Assign(4.0f));

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 16);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(1.0f));
		REQUIRE_NOTHROW(cb.At("b").Assign(2.0f));
		REQUIRE_NOTHROW(cb.At("c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("d").Assign(4.0f));

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 16);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(static_cast<short>(1)));
		REQUIRE_NOTHROW(cb.At("b").Assign(static_cast<unsigned short>(2)));
		REQUIRE_NOTHROW(cb.At("c").Assign(TRUE));

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 16);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("a").Assign(static_cast<int16_t>(1)));
		REQUIRE_NOTHROW(cb.At("c").Assign(TRUE));
		REQUIRE_NOTHROW(cb.At("b").Assign(static_cast<uint16_t>(1)));

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
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 48);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("before").Assign(glm::vec2{ 1.0f, 2.0f }));
		REQUIRE_NOTHROW(cb.At("inner.a").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("inner.b").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("inner.c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("after").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec2{ 1.0f, 2.0f }, 0)
			.Set(3.0f, 16)
			.Set(glm::vec3{ 1.0f, 2.0f, 3.0f }, 20)
			.Set(3.0f, 32)
			.Set(glm::vec3{ 1.0f, 2.0f, 3.0f }, 36);

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}

	/*SECTION("Arrays")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddElementArray("abcd", gfx::ElementType::kFloat, 4);
		auto     desc1     = desc.ToDynamicBufferDesc();
		uint32_t totalSize = desc1.GetTotalSize();
		CHECK(totalSize == 48);

		auto cb = gfx::DynamicConstantBuffer{ device, desc };
		REQUIRE_NOTHROW(cb.At("before").Assign(glm::vec2{ 1.0f, 2.0f }));
		REQUIRE_NOTHROW(cb.At("inner.a").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("inner.b").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));
		REQUIRE_NOTHROW(cb.At("inner.c").Assign(3.0f));
		REQUIRE_NOTHROW(cb.At("after").Assign(glm::vec3{ 1.0f, 2.0f, 3.0f }));

		auto raw = std::span<std::byte>(cb.GetRawData(), totalSize);

		auto expected = ByteBuffer{ totalSize };
		expected;

		CHECK(std::memcmp(raw.data(), expected.GetData(), totalSize) == 0);
	}*/
}

TEST_CASE("Constant buffer struct", "[dynamic_constant_buffer][dynamic_buffer]")
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

		REQUIRE_NOTHROW(cb["Transform.position"] = glm::vec3{ 1.0f, 2.0f, 3.0f });
		REQUIRE_NOTHROW(cb["Transform.scale"] = 2.0f);
		REQUIRE_NOTHROW(cb["id"] = 42.0f);

		auto totalSize = cb.GetDesc().GetTotalSize();

		REQUIRE(totalSize == 32);

		auto expected = ByteBuffer{ totalSize };
		expected.Set(glm::vec3{ 1.0f, 2.0f, 3.0f }, 0).Set(2.0f, 12).Set(42.0f, 16);

		auto* raw = cb.GetRawData();

		auto& dcDesc = cb.GetDesc();
		CHECK(std::memcmp(raw, expected.GetData(), dcDesc.GetTotalSize()) == 0);
		CHECK(dcDesc.GetTotalSize() % 16 == 0);
	}

	SECTION("Indexing into non-existent struct")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};
		desc.AddStruct("Transform")
			.AddElement("Transform.position", gfx::ElementType::kFloat3)
			.AddElement("id", gfx::ElementType::kFloat)
			.SetName("PerObject");

		auto cb = gfx::DynamicConstantBuffer{ device, desc };

		REQUIRE_THROWS_AS(cb.At("Transform.scale"), core::except::BerniniException);
		REQUIRE_THROWS_AS(cb.At("NonExistent.value"), core::except::BerniniException);
	}

	SECTION("Element or struct names containing dot should throw")
	{
		auto desc = gfx::DynamicConstantBufferDesc{};

		REQUIRE_THROWS_AS(
			desc.AddElement("id.dot", gfx::ElementType::kFloat),
			core::except::BerniniException);

		REQUIRE_THROWS_AS(desc.AddStruct("My.Struct"), core::except::BerniniException);
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
