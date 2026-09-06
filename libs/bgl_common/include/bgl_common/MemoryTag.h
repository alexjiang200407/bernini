#pragma once

#include <core/profiling/TaggedBytes.h>
#include <cstddef>
#include <cstdint>

namespace bgl
{
	/**
	 * The subsystem a tracked allocation is charged to.
	 *
	 * Deliberately coarse: one label per thing somebody can act on, at the granularity of Unreal's
	 * `ELLMTag`. A finer taxonomy is a set of labels nobody maintains, and an unmaintained tag
	 * reports a number nobody trusts. What no tag covers is not lost — it shows up as the residual
	 * against `core::process_memory`, which is the signal that a tag is missing.
	 *
	 * Here rather than in `core::profiling`, which owns the *mechanism*: `core` is shared by every
	 * target and has no business knowing what a mesh is. This is the engine's list, and it sits at
	 * the lowest point both things that charge memory can see — the renderer's resources and
	 * gamelib's container cache. A third charger (`bgl_wgpu`) reaches it from here too.
	 */
	enum class MemoryTag : uint8_t
	{
		kMesh,
		kAnimation,
		kTexture,
		kMaterial,
		kEnvironment,
		kShader,
		kDeviceBuffer,
		kDeviceTexture,
		kEditor,
		kCount
	};

	/** Found by ADL, as `core::profiling::MemoryTagEnum` requires. */
	constexpr std::size_t
	MemoryTagCount(MemoryTag) noexcept
	{
		return static_cast<std::size_t>(MemoryTag::kCount);
	}

	/** A literal with static lifetime: it is stored, and Tracy keeps the pointer, not the string. */
	constexpr const char*
	MemoryTagName(const MemoryTag tag) noexcept
	{
		switch (tag)
		{
		case MemoryTag::kMesh:
			return "mesh";
		case MemoryTag::kAnimation:
			return "animation";
		case MemoryTag::kTexture:
			return "texture";
		case MemoryTag::kMaterial:
			return "material";
		case MemoryTag::kEnvironment:
			return "environment";
		case MemoryTag::kShader:
			return "shader";
		case MemoryTag::kDeviceBuffer:
			return "device buffer";
		case MemoryTag::kDeviceTexture:
			return "device texture";
		case MemoryTag::kEditor:
			return "editor";
		case MemoryTag::kCount:
			break;
		}
		return "unknown";
	}

	/** The engine's charge: `core::profiling::TaggedBytes` fixed to the taxonomy above. */
	using TaggedBytes = core::profiling::TaggedBytes<MemoryTag>;
}
