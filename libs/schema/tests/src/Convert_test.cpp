#include <schema/LayoutBuilder.h>
#include <schema/Schema.h>
#include <schema/convert.h>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace schema;
using Catch::Matchers::ContainsSubstring;

/*
 * Two generations of the same struct, as two branches would leave them: V1 is what a file stores,
 * V2 what the engine wants. Between them a field was widened, one removed, one added with a
 * default, one added without, one renamed, one reordered, an array grew, and a nested struct gained
 * a field. Every rule of the converter has one of these to bite on.
 */
namespace
{
	enum class ModeV1 : uint8_t
	{
		kA,
		kB
	};

	enum class ModeV2 : uint32_t
	{
		kA,
		kB
	};

	struct InnerV1
	{
		uint16_t a;
		uint16_t b;
	};

	struct InnerV2
	{
		uint32_t a;  // widened
		uint16_t b;
		uint16_t c;  // added
	};

	struct V1
	{
		uint16_t               count;
		uint32_t               legacy;  // removed in V2
		float                  weights[2];
		InnerV1                inner;
		std::array<InnerV1, 2> inners;
		ModeV1                 mode;
		int8_t                 bias;
		uint8_t                flags;
	};

	struct V2
	{
		float                  lodBias;     // added, default 1.5f
		uint32_t               count;       // widened, and moved
		float                  weights[3];  // grew
		InnerV2                inner;
		std::array<InnerV2, 3> inners;      // grew, and the element grew
		ModeV2                 drawMode;    // renamed from mode, widened
		int32_t                bias;        // widened
		int16_t                flags;       // u8 -> i16
		uint32_t               generation;  // added, no default
	};

	static_assert(sizeof(InnerV1) == 4);
	static_assert(sizeof(InnerV2) == 8);

	Schema
	SchemaV1()
	{
		Schema schema;
		LayoutBuilder<InnerV1>(schema, "Inner")
			.AddField("a", &InnerV1::a)
			.AddField("b", &InnerV1::b)
			.Finish();
		LayoutBuilder<V1>(schema, "Thing")
			.AddField("count", &V1::count)
			.AddField("legacy", &V1::legacy)
			.AddField("weights", &V1::weights)
			.AddField("inner", &V1::inner)
			.AddField("inners", &V1::inners)
			.AddField("mode", &V1::mode)
			.AddField("bias", &V1::bias)
			.AddField("flags", &V1::flags)
			.Finish();
		return schema;
	}

	Schema
	SchemaV2()
	{
		Schema schema;
		LayoutBuilder<InnerV2>(schema, "Inner")
			.AddField("a", &InnerV2::a)
			.AddField("b", &InnerV2::b)
			.AddField("c", &InnerV2::c, uint16_t(9))
			.Finish();
		LayoutBuilder<V2>(schema, "Thing")
			.AddField("lodBias", &V2::lodBias, 1.5f)
			.AddField("count", &V2::count)
			.AddField("weights", &V2::weights, { -1.0f, -2.0f, -3.0f })
			.AddField("inner", &V2::inner)
			.AddField("inners", &V2::inners)
			.AddRenamedField("drawMode", "mode", &V2::drawMode)
			.AddField("bias", &V2::bias)
			.AddField("flags", &V2::flags)
			.AddField("generation", &V2::generation)
			.Finish();
		return schema;
	}

	V1
	SampleV1(uint16_t seed)
	{
		V1 v{};
		v.count      = seed;
		v.legacy     = 0xDEADBEEF;
		v.weights[0] = 0.25f * seed;
		v.weights[1] = 0.5f * seed;
		v.inner      = { uint16_t(seed + 1), uint16_t(seed + 2) };
		v.inners[0]  = { uint16_t(seed + 3), uint16_t(seed + 4) };
		v.inners[1]  = { uint16_t(seed + 5), uint16_t(seed + 6) };
		v.mode       = ModeV1::kB;
		v.bias       = -7;
		v.flags      = 200;
		return v;
	}

	template <typename T>
	std::span<const std::byte>
	Bytes(const T& value)
	{
		return std::as_bytes(std::span(&value, 1));
	}

	template <typename T>
	T
	As(std::span<const std::byte> bytes, size_t index = 0)
	{
		REQUIRE(bytes.size() >= (index + 1) * sizeof(T));
		T value;
		std::copy_n(
			bytes.data() + index * sizeof(T),
			sizeof(T),
			reinterpret_cast<std::byte*>(&value));
		return value;
	}
}

TEST_CASE(
	"a file at the old shape reads as the new one, field by field, by name",
	"[schema][convert]")
{
	const Schema v1 = SchemaV1();
	const Schema v2 = SchemaV2();

	const V1   in  = SampleV1(40);
	const auto out = convert(v1.GetLayoutRef("Thing"), Bytes(in), v2.GetLayoutRef("Thing"));
	REQUIRE(out.size() == sizeof(V2));
	const V2 got = As<V2>(out);

	SECTION("a widened field carries its value, wherever it moved to") { CHECK(got.count == 40); }
	SECTION("a removed field is dropped without a trace")
	{
		const std::array<uint8_t, 4> legacy = { 0xEF, 0xBE, 0xAD, 0xDE };
		CHECK(std::ranges::search(out, std::as_bytes(std::span(legacy))).empty());
	}
	SECTION("a grown array keeps what it had and takes the default for the rest")
	{
		CHECK(got.weights[0] == 10.0f);
		CHECK(got.weights[1] == 20.0f);
		CHECK(got.weights[2] == -3.0f);
	}
	SECTION("a nested struct is converted by its own layout")
	{
		CHECK(got.inner.a == 41);
		CHECK(got.inner.b == 42);
		CHECK(got.inner.c == 9);
	}
	SECTION("an array of structs converts each element and defaults the new ones")
	{
		CHECK(got.inners[0].a == 43);
		CHECK(got.inners[0].b == 44);
		CHECK(got.inners[0].c == 9);
		CHECK(got.inners[1].a == 45);
		CHECK(got.inners[1].b == 46);
		CHECK(got.inners[2].a == 0);  // an element with no default of its own reads as zero
		CHECK(got.inners[2].c == 0);
	}
	SECTION("a renamed field is found under its old name, and an enum widens as its integer")
	{
		CHECK(got.drawMode == ModeV2::kB);
	}
	SECTION("signed widens signed, unsigned widens into a wider signed")
	{
		CHECK(got.bias == -7);
		CHECK(got.flags == 200);
	}
	SECTION("a field the file lacks reads as its default, or zero without one")
	{
		CHECK(got.lodBias == 1.5f);
		CHECK(got.generation == 0);
	}
}

TEST_CASE("a run of elements converts element by element", "[schema][convert]")
{
	const Schema v1 = SchemaV1();
	const Schema v2 = SchemaV2();

	const std::array<V1, 3> in = { SampleV1(1), SampleV1(2), SampleV1(3) };
	const auto              out =
		convert(v1.GetLayoutRef("Thing"), std::as_bytes(std::span(in)), v2.GetLayoutRef("Thing"));
	REQUIRE(out.size() == 3 * sizeof(V2));
	for (uint16_t i = 0; i < 3; ++i)
	{
		const V2 got = As<V2>(out, i);
		CHECK(got.count == uint32_t(i + 1));
		CHECK(got.inner.a == uint32_t(i + 2));
		CHECK(got.lodBias == 1.5f);
	}
}

TEST_CASE("the same layout on both sides is a copy", "[schema][convert]")
{
	const Schema v1  = SchemaV1();
	const V1     in  = SampleV1(7);
	const auto   out = convert(v1.GetLayoutRef("Thing"), Bytes(in), v1.GetLayoutRef("Thing"));
	CHECK(std::ranges::equal(out, Bytes(in)));
}

TEST_CASE("the current name wins over the former one when a file carries both", "[schema][convert]")
{
	struct Old
	{
		uint32_t mode;
		uint32_t drawMode;
	};
	struct New
	{
		uint32_t drawMode;
	};
	Schema stored;
	LayoutBuilder<Old>(stored, "T")
		.AddField("mode", &Old::mode)
		.AddField("drawMode", &Old::drawMode)
		.Finish();
	Schema wanted;
	LayoutBuilder<New>(wanted, "T").AddRenamedField("drawMode", "mode", &New::drawMode).Finish();

	const Old  in  = { 1, 2 };
	const auto out = convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 0));
	CHECK(As<New>(out).drawMode == 2);
}

TEST_CASE(
	"what cannot be converted names the layout, the field and both shapes",
	"[schema][convert]")
{
	struct Stored
	{
		float    f;
		uint32_t u;
		int32_t  i;
		uint16_t s;
	};
	struct Crossed
	{
		uint32_t f;
	};
	struct Unsigned
	{
		uint32_t i;
	};
	struct Structured
	{
		uint32_t s;
	};
	Schema stored;
	LayoutBuilder<Stored>(stored, "Thing")
		.AddField("f", &Stored::f)
		.AddField("u", &Stored::u)
		.AddField("i", &Stored::i)
		.AddField("s", &Stored::s)
		.Finish();
	const Stored in{};

	SECTION("narrowing")
	{
		struct Want
		{
			uint16_t u;
		};
		Schema wanted;
		LayoutBuilder<Want>(wanted, "Thing").AddField("u", &Want::u).Finish();
		REQUIRE_THROWS_WITH(
			convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 0)),
			ContainsSubstring("Thing.u: file stores u32, engine wants u16, no conversion"));
	}
	SECTION("float to integer")
	{
		Schema wanted;
		LayoutBuilder<Crossed>(wanted, "Thing").AddField("f", &Crossed::f).Finish();
		REQUIRE_THROWS_WITH(
			convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 0)),
			ContainsSubstring("Thing.f: file stores f32, engine wants u32, no conversion"));
	}
	SECTION("signed to unsigned, however wide")
	{
		Schema wanted;
		LayoutBuilder<Unsigned>(wanted, "Thing").AddField("i", &Unsigned::i).Finish();
		REQUIRE_THROWS_WITH(
			convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 0)),
			ContainsSubstring("Thing.i: file stores i32, engine wants u32, no conversion"));
	}
	SECTION("a value that became a struct")
	{
		Schema wanted;
		LayoutBuilder<Structured>(wanted, "Sub").AddField("s", &Structured::s).Finish();
		struct Want
		{
			Structured s;
		};
		LayoutBuilder<Want>(wanted, "Thing").AddField("s", &Want::s).Finish();
		REQUIRE_THROWS_WITH(
			convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 1)),
			ContainsSubstring("Thing.s: file stores u16, engine wants struct Sub, no conversion"));
	}
	SECTION("a failure inside a nested struct names the inner layout")
	{
		struct Sub
		{
			uint32_t a;
		};
		struct Outer
		{
			Sub s;
		};
		Schema fileSide;
		LayoutBuilder<Sub>(fileSide, "Sub").AddField("a", &Sub::a).Finish();
		LayoutBuilder<Outer>(fileSide, "Thing").AddField("s", &Outer::s).Finish();
		struct SubNarrow
		{
			uint16_t a;
		};
		struct OuterNarrow
		{
			SubNarrow s;
		};
		Schema wanted;
		LayoutBuilder<SubNarrow>(wanted, "Sub").AddField("a", &SubNarrow::a).Finish();
		LayoutBuilder<OuterNarrow>(wanted, "Thing").AddField("s", &OuterNarrow::s).Finish();
		const Outer outer{};
		REQUIRE_THROWS_WITH(
			convert(LayoutRef(fileSide, 1), Bytes(outer), LayoutRef(wanted, 1)),
			ContainsSubstring("Sub.a: file stores u32, engine wants u16, no conversion"));
	}
	SECTION("an array's shape is described with its count")
	{
		struct Want
		{
			std::array<uint8_t, 4> u;
		};
		Schema wanted;
		LayoutBuilder<Want>(wanted, "Thing").AddField("u", &Want::u).Finish();
		REQUIRE_THROWS_WITH(
			convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 0)),
			ContainsSubstring("file stores u32, engine wants u8[4]"));
	}
	SECTION("bytes that are not a whole number of elements")
	{
		REQUIRE_THROWS_WITH(
			convert(
				LayoutRef(stored, 0),
				Bytes(in).first(sizeof(Stored) - 1),
				LayoutRef(stored, 0)),
			ContainsSubstring("not a whole number of 16-byte elements"));
	}
}

TEST_CASE("widens admits exactly the lossless conversions", "[schema][convert]")
{
	using enum ValueType;
	CHECK(widens(kU8, kU8));
	CHECK(widens(kU8, kU16));
	CHECK(widens(kU16, kU64));
	CHECK(widens(kU8, kI16));
	CHECK(widens(kU32, kI64));
	CHECK(widens(kI8, kI16));
	CHECK(widens(kF32, kF64));

	CHECK_FALSE(widens(kU16, kU8));
	CHECK_FALSE(widens(kU32, kI32));  // the top bit does not fit
	CHECK_FALSE(widens(kI8, kU16));
	CHECK_FALSE(widens(kI32, kF32));
	CHECK_FALSE(widens(kF32, kI64));
	CHECK_FALSE(widens(kF64, kF32));
	CHECK_FALSE(widens(kNone, kU8));
	CHECK_FALSE(widens(kU8, kNone));
}

TEST_CASE("a run of values widens element by element", "[schema][convert]")
{
	const std::array<uint16_t, 3> in = { 1, 65535, 7 };
	const auto out = convertValues(ValueType::kU16, std::as_bytes(std::span(in)), ValueType::kU32);
	REQUIRE(out.size() == 3 * sizeof(uint32_t));
	CHECK(As<uint32_t>(out, 0) == 1);
	CHECK(As<uint32_t>(out, 1) == 65535);
	CHECK(As<uint32_t>(out, 2) == 7);

	SECTION("the same type is a copy")
	{
		const auto same =
			convertValues(ValueType::kU16, std::as_bytes(std::span(in)), ValueType::kU16);
		CHECK(std::ranges::equal(same, std::as_bytes(std::span(in))));
	}
	SECTION("what does not widen is refused")
	{
		REQUIRE_THROWS_WITH(
			convertValues(ValueType::kU16, std::as_bytes(std::span(in)), ValueType::kU8),
			ContainsSubstring("file stores u16, engine wants u8, no conversion"));
	}
	SECTION("a run that is not whole is refused")
	{
		REQUIRE_THROWS_WITH(
			convertValues(ValueType::kU16, std::as_bytes(std::span(in)).first(5), ValueType::kU32),
			ContainsSubstring("5 bytes is not a whole number of u16 values"));
	}
}

TEST_CASE("a value field that became an array keeps its value as element zero", "[schema][convert]")
{
	struct Old
	{
		float x;
	};
	struct New
	{
		std::array<float, 2> x;
	};
	Schema stored;
	LayoutBuilder<Old>(stored, "T").AddField("x", &Old::x).Finish();
	Schema wanted;
	LayoutBuilder<New>(wanted, "T")
		.AddField("x", &New::x, std::array<float, 2>{ { 5.0f, 6.0f } })
		.Finish();

	const Old  in  = { 3.0f };
	const auto out = convert(LayoutRef(stored, 0), Bytes(in), LayoutRef(wanted, 0));
	CHECK(As<New>(out).x[0] == 3.0f);
	CHECK(As<New>(out).x[1] == 6.0f);
}
