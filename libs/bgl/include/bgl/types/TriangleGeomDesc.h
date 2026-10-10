#pragma once
#include <assetlib_structs/VertexLayout.h>
#include <bgl/types/MaterialHandle.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace bgl
{
	/**
	 * A triangle list built in memory, as static-mesh geometry -- see IScene::AddTriangleGeom.
	 * `vertices` is interleaved, `layout.stride` bytes a vertex, laid out as `layout` says: the
	 * attributes a cooked mesh's submesh may carry, in the same formats, a position among them. An
	 * attribute left out decodes as the renderer's default -- a normal of +z, a UV of 0, no tangent
	 * -- so a lit surface carries its normal. Borrowed for the call alone.
	 */
	struct TriangleGeomDesc
	{
		std::span<const std::byte> vertices;
		assetlib::VertexLayout     layout = {};

		// Three to a triangle, each wound counter-clockwise seen from the side its normals face.
		std::span<const uint32_t> indices;

		MaterialHandle material;

		template <typename Self>
		Self&&
		SetVertices(this Self&& self, std::span<const std::byte> value) noexcept
		{
			self.vertices = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetLayout(this Self&& self, const assetlib::VertexLayout& value) noexcept
		{
			self.layout = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetIndices(this Self&& self, std::span<const uint32_t> value) noexcept
		{
			self.indices = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaterial(this Self&& self, MaterialHandle value) noexcept
		{
			self.material = value;
			return std::forward<Self>(self);
		}
	};
}
