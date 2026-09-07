#pragma once
#include <bgl/MaterialType.h>
#include <bgl/glm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bgl
{
	// A surface is a shading function a game wrote and the engine draws through. It declares one
	// struct of parameters, and every field in that struct is one of two things: a value a material
	// sets by name, or a texture a material binds by name. What follows is that struct as the engine
	// read it back off the game's module -- the names a material writes, where each lands in the
	// record, and what an unset one gets.

	/**
	 * What a texture holds, which decides how it is sampled and how it will be baked.
	 *
	 * The surface says so by the type it declares the field as: `ColorSlot`, `DataSlot`,
	 * `NormalSlot` or `CoverageSlot` in the shader contract's `bgl/MaterialReader.slang`. Those
	 * keep the shader's word for a numbered place in the record; this names what goes in one.
	 */
	enum class SurfaceTextureKind : uint8_t
	{
		/// Colour, sRGB-encoded, its alpha kept where the layer reads one.
		kColor,

		/// Linear numbers rather than a colour: occlusion, roughness, metallic, a mask.
		kData,

		/// A tangent-space normal map, its xy as `PbrSurface::normalXY` reads them.
		kNormal,

		/// Coverage alone, for a surface whose alpha lives apart from its colour.
		kCoverage,
	};

	/**
	 * What a surface value may be. A material writes one number or a list of them.
	 *
	 * Declared in component order, and read that way in both directions: the count is the
	 * enumerator's distance from `kFloat` plus one, and a reflected component count is the
	 * enumerator that far along.
	 */
	enum class SurfaceValueType : uint8_t
	{
		kFloat,
		kFloat2,
		kFloat3,
		kFloat4,
	};

	constexpr uint32_t
	SurfaceValueComponents(SurfaceValueType type) noexcept
	{
		return static_cast<uint32_t>(type) - static_cast<uint32_t>(SurfaceValueType::kFloat) + 1;
	}

	/// One value a material sets by name, and where the block keeps it.
	struct SurfaceValue
	{
		// The field's name in the surface's parameter struct, which is what a material's
		// `parameters` writes to reach it.
		std::string name;

		SurfaceValueType type = SurfaceValueType::kFloat;

		// From the start of the block, not of the record.
		uint32_t byteOffset = 0;

		// What a material that does not name this value gets. Components past the type's are zero.
		glm::vec4 defaultValue = glm::vec4(0.0f);
	};

	/// One texture a material binds by name, sampled through the index the engine packs into it.
	struct SurfaceTexture
	{
		// The field's name in the surface's parameter struct, which is what a material's `textures`
		// writes to bind a texture to it.
		std::string name;

		SurfaceTextureKind kind = SurfaceTextureKind::kColor;

		// Which of the record's texture handles this one samples, and the value written into the
		// field at `byteOffset`.
		uint32_t index = 0;

		// From the start of the block, as SurfaceValue::byteOffset is.
		uint32_t byteOffset = 0;
	};

	/**
	 * The surface's `Params` struct as the engine read it back: everything a material may set, where
	 * each of them lands, and what an unset one gets. This is what packs a record and what reads one
	 * back, and it is the whole of what a packer needs -- which surface it belongs to is not.
	 */
	struct SurfaceParams
	{
		// The block's own size, past the engine's fixed part of the record.
		uint32_t byteSize = 0;

		std::vector<SurfaceValue>   values;
		std::vector<SurfaceTexture> textures;
	};

	/**
	 * A surface the client registered, as the engine reads it. Reflected off the game's own Slang
	 * module, so the layout comes from the code that reads it rather than from a manifest beside it.
	 */
	struct SurfaceType
	{
		// What a material names to draw with this surface, normally its module file's stem.
		std::string name;

		// What a record of this surface is tagged with: one of the reserved game kinds from
		// `MaterialType::kGameStart`, and the kind a material handle of this surface carries.
		// Assigned by registration; reflection leaves it invalid.
		MaterialType kind = MaterialType::kInvalid;

		SurfaceParams params;
	};
}
