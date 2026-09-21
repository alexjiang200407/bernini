#include "foot_ik_weights.h"
#include <algorithm>
#include <bgl/types/FootIKDesc.h>

namespace editor
{
	bgl::FootIKDesc
	FootIKForSliders(const int positionPercent, const int rotationPercent)
	{
		const auto weight = [](const int percent) {
			return static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f;
		};
		return bgl::FootIKDesc::Constant(weight(positionPercent), weight(rotationPercent));
	}
}
