#include <assetlib_schema/Schema.h>
#include <assetlib_schema/SchemaBuilder.h>

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceStamp.h>
#include <assetlib_structs/VertexLayout.h>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;
using namespace assetlib::schema;
using Catch::Matchers::ContainsSubstring;

namespace
{
	enum class Colour : uint16_t
	{
		kRed,
		kGreen
	};

	struct Inner
	{
		uint8_t  a;
		uint16_t b;
	};

	struct Outer
	{
		Inner                inner;
		float                f;
		glm::vec3            v;
		Colour               colour;
		std::array<Inner, 2> inners;
		uint64_t             wide;
	};

	static_assert(sizeof(Inner) == 4);
	static_assert(sizeof(Outer) == 40);

	uint32_t
	RegisterInner(Schema& schema)
	{
		return LayoutBuilder<Inner>(schema, "Inner")
		    .AddField("a", &Inner::a)
		    .AddField("b", &Inner::b)
		    .Finish();
	}

	const Field&
	FieldOf(const Layout& type, std::string_view name)
	{
		const auto it = std::ranges::find(type.fields, name, &Field::name);
		REQUIRE(it != type.fields.end());
		return *it;
	}

	/** The disk PODs of every chunked container, registered as their io will register them. */
	Schema
	RealSchema()
	{
		Schema schema;
		LayoutBuilder<Transform>(schema, "Transform")
			.AddField("translation", &Transform::translation)
			.AddField("rotation", &Transform::rotation)
			.AddField("scale", &Transform::scale)
			.Finish();
		LayoutBuilder<Node>(schema, "Node")
			.AddField("localTransform", &Node::localTransform)
			.AddField("parent", &Node::parent)
			.AddField("firstChild", &Node::firstChild)
			.AddField("nextSibling", &Node::nextSibling)
			.AddField("mesh", &Node::mesh)
			.AddField("nameOffset", &Node::nameOffset)
			.Finish();
		LayoutBuilder<Mesh>(schema, "Mesh")
			.AddField("firstSubmesh", &Mesh::firstSubmesh)
			.AddField("submeshCount", &Mesh::submeshCount)
			.AddField("nameOffset", &Mesh::nameOffset)
			.Finish();
		LayoutBuilder<VertexAttribute>(schema, "VertexAttribute")
			.AddField("semantic", &VertexAttribute::semantic)
			.AddField("format", &VertexAttribute::format)
			.AddField("offset", &VertexAttribute::offset)
			.Finish();
		LayoutBuilder<VertexLayout>(schema, "VertexLayout")
			.AddField("attributes", &VertexLayout::attributes)
			.AddField("attributeCount", &VertexLayout::attributeCount)
			.AddField("stride", &VertexLayout::stride)
			.Finish();
		LayoutBuilder<Submesh>(schema, "Submesh")
			.AddField("layout", &Submesh::layout)
			.AddField("vertexByteOffset", &Submesh::vertexByteOffset)
			.AddField("vertexCount", &Submesh::vertexCount)
			.AddField("indexByteOffset", &Submesh::indexByteOffset)
			.AddField("indexCount", &Submesh::indexCount)
			.AddField("indexType", &Submesh::indexType)
			.AddField("firstMeshlet", &Submesh::firstMeshlet)
			.AddField("meshletCount", &Submesh::meshletCount)
			.AddField("material", &Submesh::material)
			.AddField("aabbMin", &Submesh::aabbMin)
			.AddField("aabbMax", &Submesh::aabbMax)
			.AddField("nameOffset", &Submesh::nameOffset)
			.Finish();
		LayoutBuilder<Meshlet>(schema, "Meshlet")
			.AddField("vertexOffset", &Meshlet::vertexOffset)
			.AddField("triangleOffset", &Meshlet::triangleOffset)
			.AddField("vertexCount", &Meshlet::vertexCount)
			.AddField("triangleCount", &Meshlet::triangleCount)
			.AddField("boundingCenter", &Meshlet::boundingCenter)
			.AddField("boundingRadius", &Meshlet::boundingRadius)
			.Finish();
		LayoutBuilder<Bone>(schema, "Bone")
			.AddField("bindPose", &Bone::bindPose)
			.AddField("inverseBind", &Bone::inverseBind)
			.AddField("parent", &Bone::parent)
			.AddField("nameOffset", &Bone::nameOffset)
			.Finish();
		LayoutBuilder<AnimationClip>(schema, "AnimationClip")
			.AddField("nameOffset", &AnimationClip::nameOffset)
			.AddField("firstSample", &AnimationClip::firstSample)
			.AddField("frameCount", &AnimationClip::frameCount)
			.AddField("sampleRate", &AnimationClip::sampleRate)
			.AddField("duration", &AnimationClip::duration)
			.AddField("rootMotion", &AnimationClip::rootMotion)
			.AddField("locomotionSpeed", &AnimationClip::locomotionSpeed)
			.AddField("loop", &AnimationClip::loop)
			.Finish();
		LayoutBuilder<SourceStamp>(schema, "SourceStamp")
			.AddField("size", &SourceStamp::size)
			.AddField("hash", &SourceStamp::hash)
			.Finish();
		return schema;
	}
}

TEST_CASE("the builder reads type, value type, count and offset off the member", "[schema]")
{
	Schema schema;
	RegisterInner(schema);
	LayoutBuilder<Outer>(schema, "Outer")
		.AddField("inner", &Outer::inner)
		.AddField("f", &Outer::f)
		.AddField("v", &Outer::v)
		.AddField("colour", &Outer::colour)
		.AddField("inners", &Outer::inners)
		.AddField("wide", &Outer::wide)
		.Finish();

	const Layout& outer = *schema.Find("Outer");
	REQUIRE(outer.size == sizeof(Outer));
	REQUIRE(outer.fields.size() == 6);

	const Field& inner = FieldOf(outer, "inner");
	CHECK(inner.type == Type::kStruct);
	CHECK(inner.valueType == ValueType::kNone);
	CHECK(inner.layoutIndex == 0);
	CHECK(inner.offset == 0);
	CHECK(schema.GetFieldSize(inner) == sizeof(Inner));

	CHECK(FieldOf(outer, "f").type == Type::kValue);
	CHECK(FieldOf(outer, "f").valueType == ValueType::kF32);
	CHECK(FieldOf(outer, "f").offset == 4);

	const Field& v = FieldOf(outer, "v");
	CHECK(v.type == Type::kArray);
	CHECK(v.valueType == ValueType::kF32);
	CHECK(v.count == 3);
	CHECK(v.offset == 8);

	CHECK(FieldOf(outer, "colour").type == Type::kValue);
	CHECK(FieldOf(outer, "colour").valueType == ValueType::kU16);
	CHECK(FieldOf(outer, "colour").offset == 20);

	const Field& inners = FieldOf(outer, "inners");
	CHECK(inners.type == Type::kArray);
	CHECK(inners.valueType == ValueType::kNone);
	CHECK(inners.layoutIndex == 0);
	CHECK(inners.count == 2);
	CHECK(inners.offset == 22);
	CHECK(schema.GetFieldSize(inners) == 2 * sizeof(Inner));

	CHECK(FieldOf(outer, "wide").type == Type::kValue);
	CHECK(FieldOf(outer, "wide").valueType == ValueType::kU64);
	CHECK(FieldOf(outer, "wide").offset == 32);
	CHECK(schema.GetLayoutAlignment(1) == 8);
}

TEST_CASE("a forgotten field is refused at registration", "[schema]")
{
	Schema schema;
	RegisterInner(schema);

	SECTION("a gap the size of a field in the middle")
	{
		REQUIRE_THROWS_WITH(
			LayoutBuilder<Outer>(schema, "Outer")
				.AddField("inner", &Outer::inner)
				.AddField("v", &Outer::v)  // f at 4 is missing
				.AddField("colour", &Outer::colour)
				.AddField("inners", &Outer::inners)
				.AddField("wide", &Outer::wide)
				.Finish(),
			ContainsSubstring("4 bytes at 4 belong to no field"));
	}

	SECTION("a gap the size of a field at the end")
	{
		REQUIRE_THROWS_WITH(
			LayoutBuilder<Outer>(schema, "Outer")
				.AddField("inner", &Outer::inner)
				.AddField("f", &Outer::f)
				.AddField("v", &Outer::v)
				.AddField("colour", &Outer::colour)
				.AddField("inners", &Outer::inners)
				.Finish(),
			ContainsSubstring("10 bytes at 30 belong to no field"));
	}

	SECTION("a byte the alignment padding would hide is still refused")
	{
		// colour is the u16 at 20 and inners follows at 22, 2-aligned: a 2-byte gap there is a
		// missing u16, not padding.
		REQUIRE_THROWS_WITH(
			LayoutBuilder<Outer>(schema, "Outer")
				.AddField("inner", &Outer::inner)
				.AddField("f", &Outer::f)
				.AddField("v", &Outer::v)
				.AddField("inners", &Outer::inners)
				.AddField("wide", &Outer::wide)
				.Finish(),
			ContainsSubstring("2 bytes at 20 belong to no field"));
	}
}

TEST_CASE("alignment padding is not a missing field", "[schema]")
{
	// Inner is {u8 a; u16 b}: one byte of padding at 1 before b, none after.
	Schema schema;
	REQUIRE_NOTHROW(RegisterInner(schema));
	CHECK(schema.GetLayoutAlignment(0) == 2);
}

TEST_CASE("a struct field must name a type registered before it", "[schema]")
{
	Schema schema;
	REQUIRE_THROWS_WITH(
		LayoutBuilder<Outer>(schema, "Outer").AddField("inner", &Outer::inner),
		ContainsSubstring("Outer.inner is a struct the schema does not hold"));
}

TEST_CASE("Add refuses what a builder cannot produce", "[schema]")
{
	Schema schema;
	RegisterInner(schema);

	SECTION("overlapping fields")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 4;
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU32, .offset = 0 });
		layout.fields.push_back({ .name = "b", .valueType = ValueType::kU16, .offset = 2 });
		REQUIRE_THROWS_WITH(schema.Add(std::move(layout)), ContainsSubstring("overlaps"));
	}

	SECTION("a duplicate field name")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 8;
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU32, .offset = 0 });
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU32, .offset = 4 });
		REQUIRE_THROWS_WITH(schema.Add(std::move(layout)), ContainsSubstring("declared twice"));
	}

	SECTION("a duplicate type name")
	{
		Layout layout;
		layout.name = "Inner";
		layout.size = 4;
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU32, .offset = 0 });
		REQUIRE_THROWS_WITH(schema.Add(std::move(layout)), ContainsSubstring("registered twice"));
	}

	SECTION("a struct field naming a type not yet registered")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 4;
		layout.fields.push_back(
			{ .name = "x", .type = Type::kStruct, .layoutIndex = 7, .offset = 0 });
		REQUIRE_THROWS_WITH(
			schema.Add(std::move(layout)),
			ContainsSubstring("not registered before it"));
	}

	SECTION("a misaligned field")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 8;
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU8, .offset = 0 });
		layout.fields.push_back({ .name = "b", .valueType = ValueType::kU32, .offset = 1 });
		REQUIRE_THROWS_WITH(schema.Add(std::move(layout)), ContainsSubstring("not 4-aligned"));
	}

	SECTION("a value field that names a layout, a struct field that names none")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 4;
		layout.fields.push_back(
			{ .name = "a", .type = Type::kValue, .valueType = ValueType::kU32, .layoutIndex = 0 });
		REQUIRE_THROWS_WITH(schema.Add(std::move(layout)), ContainsSubstring("names no layout"));

		Layout other;
		other.name = "U";
		other.size = 4;
		other.fields.push_back(
			{ .name = "a", .type = Type::kStruct, .valueType = ValueType::kU32 });
		REQUIRE_THROWS_WITH(schema.Add(std::move(other)), ContainsSubstring("names its layout"));
	}

	SECTION("a count on anything but an array")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 8;
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU32, .count = 2 });
		REQUIRE_THROWS_WITH(
			schema.Add(std::move(layout)),
			ContainsSubstring("only an array has a count other than 1"));
	}

	SECTION("a default of the wrong size")
	{
		REQUIRE_THROWS_WITH(
			LayoutBuilder<Inner>(schema, "Inner2")
				.AddField("a", &Inner::a)
				.DefaultTo(uint32_t(0))
				.AddField("b", &Inner::b)
				.Finish(),
			ContainsSubstring("default is 4 bytes, the field is 1"));
	}
}

TEST_CASE("RenamedFrom and DefaultTo qualify the field just declared", "[schema]")
{
	Schema schema;
	LayoutBuilder<Inner>(schema, "Inner")
		.AddField("a", &Inner::a)
		.AddField("width", &Inner::b)
		.RenamedFrom("b")
		.DefaultTo(uint16_t(7))
		.Finish();

	const Field& width = FieldOf(*schema.Find("Inner"), "width");
	CHECK(width.formerly == "b");
	REQUIRE(width.defaultValue.size() == 2);
	uint16_t value = 0;
	std::copy_n(width.defaultValue.data(), 2, reinterpret_cast<std::byte*>(&value));
	CHECK(value == 7);
}

TEST_CASE("every disk POD's builder covers its sizeof", "[schema]")
{
	Schema schema;
	REQUIRE_NOTHROW(schema = RealSchema());
	CHECK(schema.GetLayouts().size() == 10);
	CHECK(schema.Find("Submesh")->size == sizeof(Submesh));
	CHECK(schema.Find("Node")->size == sizeof(Node));
	CHECK(schema.Find("Bone")->size == sizeof(Bone));

	// The struct fields resolved to the earlier registrations.
	CHECK(
		FieldOf(*schema.Find("Submesh"), "layout").layoutIndex ==
		*schema.FindIndex(typeid(VertexLayout)));
	CHECK(
		FieldOf(*schema.Find("Bone"), "bindPose").layoutIndex ==
		*schema.FindIndex(typeid(Transform)));
	CHECK(
		FieldOf(*schema.Find("VertexLayout"), "attributes").count == VertexLayout::c_MaxAttributes);
	CHECK(FieldOf(*schema.Find("Bone"), "inverseBind").count == 16);
}

TEST_CASE("a layout is referred to by name, and the reference knows its schema", "[schema]")
{
	const Schema    schema  = RealSchema();
	const LayoutRef submesh = schema.GetLayoutRef("Submesh");
	CHECK(&submesh.GetSchema() == &schema);
	CHECK(submesh.GetLayout().name == "Submesh");
	CHECK(submesh.GetIndex() == *schema.FindIndex(typeid(Submesh)));
	REQUIRE_THROWS_WITH(schema.GetLayoutRef("Nope"), ContainsSubstring("no layout named Nope"));
}

TEST_CASE("a schema round-trips through its byte form", "[schema]")
{
	const Schema schema = RealSchema();
	const auto   bytes  = serialize(schema);

	Schema back;
	REQUIRE_NOTHROW(back = deserialize(bytes));
	REQUIRE(back.GetLayouts().size() == schema.GetLayouts().size());
	for (size_t i = 0; i < schema.GetLayouts().size(); ++i)
	{
		const Layout& a = schema.GetLayouts()[i];
		const Layout& b = back.GetLayouts()[i];
		CHECK(a.name == b.name);
		CHECK(a.size == b.size);
		REQUIRE(a.fields.size() == b.fields.size());
		for (size_t j = 0; j < a.fields.size(); ++j)
		{
			CHECK(a.fields[j].name == b.fields[j].name);
			CHECK(a.fields[j].type == b.fields[j].type);
			CHECK(a.fields[j].valueType == b.fields[j].valueType);
			CHECK(a.fields[j].layoutIndex == b.fields[j].layoutIndex);
			CHECK(a.fields[j].offset == b.fields[j].offset);
			CHECK(a.fields[j].count == b.fields[j].count);
		}
	}

	// Byte-exact the second time, so the same schema written by two builds is the same bytes.
	CHECK(serialize(back) == bytes);

	// What a reader cannot use is not carried.
	CHECK_FALSE(back.FindIndex(typeid(Submesh)).has_value());
}

TEST_CASE("a truncated or foreign schema table is refused", "[schema]")
{
	const auto bytes = serialize(RealSchema());

	SECTION("cut inside the field records")
	{
		REQUIRE_THROWS_WITH(
			deserialize(std::span(bytes).first(bytes.size() / 2)),
			ContainsSubstring("unexpected end of stream"));
	}

	SECTION("cut inside the pool")
	{
		REQUIRE_THROWS_WITH(
			deserialize(std::span(bytes).first(bytes.size() - 1)),
			ContainsSubstring("unexpected end of stream"));
	}

	SECTION("a version this reader does not know")
	{
		auto bumped = bytes;
		bumped[0]   = std::byte{ 9 };
		REQUIRE_THROWS_WITH(deserialize(bumped), ContainsSubstring("unsupported version 9"));
	}

	SECTION("a table that reads back but describes an impossible type")
	{
		Layout layout;
		layout.name = "T";
		layout.size = 4;
		layout.fields.push_back({ .name = "a", .valueType = ValueType::kU32, .offset = 0 });
		Schema one;
		one.Add(std::move(layout));
		auto tampered = serialize(one);
		// The one field's offset sits after the header (16), the layout record (16) and the field
		// record's name (4): move it, and the layout no longer tiles.
		tampered[16 + 16 + 4] = std::byte{ 8 };
		REQUIRE_THROWS_WITH(deserialize(tampered), ContainsSubstring("belong to no field"));
	}
}
