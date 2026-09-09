#include "blend_edits.h"

#include <algorithm>
#include <assetlib/blend.h>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>

namespace editor
{
	namespace
	{
		// Well inside the range a double represents every integer of, so the rounding below is
		// exact and never undefined.
		constexpr double c_RoundLimit = 1e15;
	}

	size_t
	InsertionIndex(std::span<const assetlib::BlendSpaceSample> run, const float parameter) noexcept
	{
		const auto at =
			std::ranges::lower_bound(run, parameter, {}, &assetlib::BlendSpaceSample::parameter);

		return static_cast<size_t>(at - run.begin());
	}

	bool
	CanInsertAt(
		std::span<const assetlib::BlendSpaceSample> run,
		const float                                 parameter,
		const float                                 precision) noexcept
	{
		if (!std::isfinite(parameter) || !(precision > 0.0f))
			return false;

		// The step each value displays as. An integer comparison, because the distance between two
		// adjacent steps is not exactly `precision` in binary and a subtraction would refuse the
		// next value the box can offer.
		//
		// The ratio is held inside what a long long can represent first. Both doors admit any finite
		// parameter, so a hand-written `.bblend` may carry 1e20, and rounding that over a precision
		// of 0.01 is undefined rather than merely large. Two parameters that saturate compare equal,
		// which is the truth about them: at that magnitude a float's own step is far wider than any
		// precision a box could show.
		const auto step = [precision](const float value) {
			const double ratio = static_cast<double>(value) / static_cast<double>(precision);
			return std::llround(std::clamp(ratio, -c_RoundLimit, c_RoundLimit));
		};

		const size_t at    = InsertionIndex(run, parameter);
		const auto   shown = step(parameter);

		if (at > 0 && step(run[at - 1].parameter) == shown)
			return false;

		return at >= run.size() || step(run[at].parameter) != shown;
	}

	float
	ClampedParameter(
		std::span<const assetlib::BlendSpaceSample> run,
		const size_t                                index,
		const float                                 parameter,
		const float                                 precision) noexcept
	{
		if (index >= run.size())
			return parameter;

		if (!std::isfinite(parameter))
			return run[index].parameter;

		// Open at both ends: the run's own extent is what a person authors by dragging the first
		// and last samples, so only the interior is fenced.
		const float below = index > 0 ? run[index - 1].parameter + precision :
		                                -std::numeric_limits<float>::infinity();
		const float above = index + 1 < run.size() ? run[index + 1].parameter - precision :
		                                             std::numeric_limits<float>::infinity();

		// A run whose neighbours are already closer together than two steps has no room between
		// them; holding the sample still beats moving it to a value that breaks the order.
		if (above < below)
			return run[index].parameter;

		return std::clamp(parameter, below, above);
	}

	bool
	CanRemoveSample(std::span<const assetlib::BlendSpaceSample> run) noexcept
	{
		return run.size() > 2;
	}

	bool
	CanNameSpace(
		std::span<const assetlib::BlendSpace> existing,
		const std::string_view                name) noexcept
	{
		if (name.empty())
			return false;

		return std::ranges::none_of(existing, [name](const assetlib::BlendSpace& space) {
			return space.name == name;
		});
	}

	float
	ParameterForTick(const float min, const float max, const int ticks, const int tick) noexcept
	{
		if (ticks <= 0 || !(max > min))
			return min;

		const float t = static_cast<float>(std::clamp(tick, 0, ticks)) / static_cast<float>(ticks);
		return std::lerp(min, max, t);
	}

	int
	TickForParameter(
		const float min,
		const float max,
		const int   ticks,
		const float parameter) noexcept
	{
		if (ticks <= 0 || !(max > min) || !std::isfinite(parameter))
			return 0;

		// Clamped before it is scaled, not after: a parameter far outside the run makes `t * ticks`
		// larger than a long can hold, and rounding that is undefined rather than simply out of
		// range.
		const float t = std::clamp((parameter - min) / (max - min), 0.0f, 1.0f);
		return std::clamp(static_cast<int>(std::lround(t * static_cast<float>(ticks))), 0, ticks);
	}
}
