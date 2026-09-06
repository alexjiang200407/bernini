#pragma once

#include <assetlib_structs/VertexLayout.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

/**
 * Describing one interleaved vertex of a synthesized `.bmesh`: where each field lands, and how to
 * write it there.
 *
 * `assetlib` exposes neither -- a `BMesh` carries its vertices as bytes plus a layout, and every
 * fixture in the suite that builds one by hand owes both halves. Shared so a fixture describes what
 * makes it different rather than restating the layout it has in common.
 */
namespace bgl::test
{
	/** position(12) + normal(12) + uv(8) + tangent(16) + joints0(8) + weights0(8). */
	constexpr uint32_t c_SkinnedVertexStride = 64;

	/**
	 * Position, normal, UV, tangent, joints and weights, interleaved in that order -- what a skinned
	 * submesh needs and the layout every skinned fixture here uses.
	 */
	[[nodiscard]] inline assetlib::VertexLayout
	SkinnedVertexLayout()
	{
		auto layout           = assetlib::VertexLayout();
		layout.attributeCount = 6;
		layout.stride         = c_SkinnedVertexStride;
		layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                      assetlib::VertexFormat::kFloat32x3,
			                      0 };
		layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                      assetlib::VertexFormat::kFloat32x3,
			                      12 };
		layout.attributes[2]  = { assetlib::VertexSemantic::kTexCoord0,
			                      assetlib::VertexFormat::kFloat32x2,
			                      24 };
		layout.attributes[3]  = { assetlib::VertexSemantic::kTangent,
			                      assetlib::VertexFormat::kFloat32x4,
			                      32 };
		layout.attributes[4]  = { assetlib::VertexSemantic::kJoints0,
			                      assetlib::VertexFormat::kUint16x4,
			                      48 };
		layout.attributes[5]  = { assetlib::VertexSemantic::kWeights0,
			                      assetlib::VertexFormat::kUnorm16x4,
			                      56 };
		return layout;
	}

	inline void
	PutFloats(std::vector<std::byte>& bytes, size_t at, std::span<const float> values)
	{
		std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(float));
	}

	inline void
	PutU16x4(std::vector<std::byte>& bytes, size_t at, std::span<const uint16_t> values)
	{
		std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(uint16_t));
	}
}
