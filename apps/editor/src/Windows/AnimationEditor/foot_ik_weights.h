#pragma once

#include <bgl/types/FootIKDesc.h>

namespace editor
{
	/**
	 * The foot-IK record the Animation panel's two sliders stand for: every leg held at
	 * `positionPercent` and `rotationPercent`, clamped to 0..100.
	 *
	 * Constants, never a ramp. The panel's clock is the transport's clip time, wrapped over the
	 * clip period, so a ramp stamped in it would re-read its start after every wrap; what a slider
	 * previews is a partial weight held still, and the fade is the runtime's to show.
	 */
	[[nodiscard]] bgl::FootIKDesc
	FootIKForSliders(int positionPercent, int rotationPercent);
}
