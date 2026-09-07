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
	// sets by name, or a texture slot a material binds a texture to by name. What follows is that
	// declaration as the engine read it back off the game's module -- the names a material writes,
	// where each lands in the record, and what an unset one gets.

	/**
	 * What a slot's texture holds, which decides how it is sampled and how it will be baked.
	 *
	 * The surface says so by the type it declares the field as: `ColorSlot`, `DataSlot`,
	 * `NormalSlot` or `CoverageSlot` in the shader contract's `bgl/MaterialReader.slang`.
	 */
	enum class SurfaceSlotKind : uint8_t
	{
		kColor,
		kData,
		kNormal,
		kCoverage,
	};

	/**
	 * What a surface parameter may be. A material writes one number or a list of them.
	 *
	 * Declared in component order, and read that way in both directions: the count is the
	 * enumerator's distance from `kFloat` plus one, and a reflected component count is the
	 * enumerator that far along.
	 */
	enum class SurfaceParameterType : uint8_t
	{
		kFloat,
		kFloat2,
		kFloat3,
		kFloat4,
	};

	constexpr uint32_t
	SurfaceParameterComponents(SurfaceParameterType type) noexcept
	{
		return static_cast<uint32_t>(type) - static_cast<uint32_t>(SurfaceParameterType::kFloat) +
		       1;
	}

	/// One value a material sets by name, and where the record keeps it.
	struct SurfaceParameter
	{
		// The field's name in the surface's parameter struct, which is what a material's
		// `parameters` writes to reach it.
		std::string name;

		SurfaceParameterType type = SurfaceParameterType::kFloat;

		// From the start of the parameter block, not of the record.
		uint32_t offset = 0;

		// What a material that does not name this parameter gets. Components past the type's are
		// zero.
		glm::vec4 defaultValue = glm::vec4(0.0f);
	};

	/// One texture a material binds by name, sampled through the index the engine packs into it.
	struct SurfaceSlot
	{
		// The field's name in the surface's parameter struct, which is what a material's `slots`
		// writes to bind a texture to it.
		std::string name;

		SurfaceSlotKind kind = SurfaceSlotKind::kColor;

		// Which of the record's texture handles this slot samples, and the value written into the
		// field at `offset`.
		uint32_t index = 0;

		// From the start of the parameter block, as SurfaceParameter::offset is.
		uint32_t offset = 0;
	};

	/**
	 * A surface the client registered, as the engine reads it: what a material may set and where
	 * each value lands in the record. Reflected off the game's own Slang module, so the layout
	 * comes from the code that reads it.
	 */
	struct SurfaceType
	{
		// What a material names to draw with this surface, normally its module file's stem.
		std::string name;

		// What a record of this surface is tagged with: one of the reserved game kinds from
		// `MaterialType::kGameStart`, and the kind a material handle of this surface carries.
		// Assigned by registration; reflection leaves it invalid.
		MaterialType kind = MaterialType::kInvalid;

		// Bytes of parameter block, past the engine's fixed part of the record.
		uint32_t paramsSize = 0;

		std::vector<SurfaceParameter> parameters;
		std::vector<SurfaceSlot>      slots;
	};
}
