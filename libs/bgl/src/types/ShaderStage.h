#pragma once

namespace bgl
{
	/**
	 * A shader stage a pipeline compiles and binds separately.
	 *
	 * The values are serialized into the Metal shader cache, so their order is part of that format
	 * and an insertion in the middle needs the cache's version bumped.
	 */
	enum class ShaderStage : uint32_t
	{
		kCompute,
		kObject,
		kMesh,
		kFragment,

		kCount,
	};

	inline constexpr size_t c_ShaderStageCount = static_cast<size_t>(ShaderStage::kCount);
}
