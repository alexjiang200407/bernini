#pragma once
#include <bgl/glm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bgl
{
	/// What a slot's texture holds, read off the field's declared type in the surface's parameters.
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
	enum class SurfaceParamType : uint8_t
	{
		kFloat,
		kFloat2,
		kFloat3,
		kFloat4,
	};

	constexpr uint32_t
	SurfaceParamComponents(SurfaceParamType type) noexcept
	{
		return static_cast<uint32_t>(type) - static_cast<uint32_t>(SurfaceParamType::kFloat) + 1;
	}

	/// A value a material sets by name, at a fixed place in the record's parameter block.
	struct SurfaceParam
	{
		std::string      name;
		SurfaceParamType type = SurfaceParamType::kFloat;

		// From the start of the parameter block, not of the record.
		uint32_t offset = 0;

		// What a material that does not name this parameter gets. Components past the type's are
		// zero.
		glm::vec4 defaultValue = glm::vec4(0.0f);
	};

	/// A texture a material binds by name, sampled through the index the engine packs into it.
	struct SurfaceSlot
	{
		std::string     name;
		SurfaceSlotKind kind = SurfaceSlotKind::kColor;

		// Which of the record's texture handles this slot samples, and the value written into the
		// field at `offset`.
		uint32_t index = 0;

		// From the start of the parameter block, as SurfaceParam::offset is.
		uint32_t offset = 0;
	};

	/**
	 * A surface the client registered, as the engine reads it: what a material may set and where
	 * each value lands in the record. Reflected off the game's own Slang module, so the layout
	 * comes from the code that reads it.
	 */
	struct SurfaceType
	{
		// What a material names to draw with this surface.
		std::string name;

		// Which reserved slot the surface was registered into. Assigned by registration, not by
		// reflection.
		uint32_t slot = 0;

		// Bytes of parameter block, past the engine's fixed part of the record.
		uint32_t paramsSize = 0;

		std::vector<SurfaceParam> parameters;
		std::vector<SurfaceSlot>  slots;
	};
}
