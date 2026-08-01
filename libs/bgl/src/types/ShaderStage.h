#pragma once

namespace bgl
{
	enum class ShaderStage : uint32_t
	{
		kCompute,
		kAmplification,
		kMesh,
		kPixel,

		kCount,
	};

	inline constexpr size_t c_ShaderStageCount = static_cast<size_t>(ShaderStage::kCount);
}
