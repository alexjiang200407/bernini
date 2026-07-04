#include <assetlib/bmesh/VertexLayout.h>

using namespace assetlib::bmesh;

TEST_CASE("formatSize reports the byte size of each vertex format", "[bmesh][layout]")
{
	REQUIRE(formatSize(VertexFormat::kFloat32x2) == 8);
	REQUIRE(formatSize(VertexFormat::kFloat32x3) == 12);
	REQUIRE(formatSize(VertexFormat::kFloat32x4) == 16);
	REQUIRE(formatSize(VertexFormat::kUnorm8x4) == 4);
	REQUIRE(formatSize(VertexFormat::kUnorm16x2) == 4);
	REQUIRE(formatSize(VertexFormat::kUnorm16x4) == 8);
	REQUIRE(formatSize(VertexFormat::kUint16x4) == 8);
}

TEST_CASE("a packed layout's stride equals the sum of its attribute sizes", "[bmesh][layout]")
{
	// The default import layout: pos(12) + normal(12) + uv(8) + tangent(16) = 48.
	VertexLayout layout{};
	layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
	layout.attributes[1]  = { VertexSemantic::kNormal, VertexFormat::kFloat32x3, 12 };
	layout.attributes[2]  = { VertexSemantic::kTexCoord0, VertexFormat::kFloat32x2, 24 };
	layout.attributes[3]  = { VertexSemantic::kTangent, VertexFormat::kFloat32x4, 32 };
	layout.attributeCount = 4;
	layout.stride         = 48;

	uint32_t sum = 0;
	for (uint8_t i = 0; i < layout.attributeCount; ++i)
		sum += formatSize(layout.attributes[i].format);

	REQUIRE(sum == layout.stride);
}
