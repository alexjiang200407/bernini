#include "blend_edits.h"

#include <algorithm>
#include <assetlib/blend.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gamelib/BlendSpaceInfo.h>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "Windows/AnimationEditor/PlaybackTransport.h"

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

	std::optional<size_t>
	MoveSample(
		std::vector<assetlib::BlendSpaceSample>& run,
		const size_t                             index,
		const float                              parameter,
		const float                              precision)
	{
		if (index >= run.size())
			return std::nullopt;

		// Taken out first so the sample is not its own neighbour: a nudge that still displays as its
		// old value is a move, not a duplicate.
		assetlib::BlendSpaceSample moved = std::move(run[index]);
		run.erase(run.begin() + static_cast<ptrdiff_t>(index));

		if (!CanInsertAt(run, parameter, precision))
		{
			run.insert(run.begin() + static_cast<ptrdiff_t>(index), std::move(moved));
			return std::nullopt;
		}

		const size_t at = InsertionIndex(run, parameter);
		moved.parameter = parameter;
		run.insert(run.begin() + static_cast<ptrdiff_t>(at), std::move(moved));
		return at;
	}

	bool
	ReplaceSampleClip(
		const std::span<assetlib::BlendSpaceSample> run,
		const size_t                                index,
		const std::string_view                      clip)
	{
		if (index >= run.size())
			return false;

		run[index].clip = clip;
		return true;
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

	bool
	IsParameterMove(
		const std::span<const assetlib::BlendSpace> before,
		const std::span<const assetlib::BlendSpace> after) noexcept
	{
		if (before.size() != after.size())
			return false;

		for (size_t s = 0; s < before.size(); ++s)
		{
			if (before[s].name != after[s].name ||
			    before[s].samples.size() != after[s].samples.size())
				return false;

			for (size_t i = 0; i < before[s].samples.size(); ++i)
				if (before[s].samples[i].clip != after[s].samples[i].clip)
					return false;
		}

		return true;
	}

	bool
	ApplyParameters(
		const std::span<const assetlib::BlendSpace> authored,
		const std::span<game::BlendSpaceInfo>       live) noexcept
	{
		if (authored.size() != live.size())
			return false;

		for (size_t s = 0; s < authored.size(); ++s)
			if (authored[s].samples.size() != live[s].samples.size())
				return false;

		// Checked in full before a byte moves, for the reason IScene::SetRigBlendParameters is: a
		// half-written run is one nothing refuses and nobody authored.
		for (size_t s = 0; s < authored.size(); ++s)
			for (size_t i = 0; i < authored[s].samples.size(); ++i)
				live[s].samples[i].parameter = authored[s].samples[i].parameter;

		return true;
	}

	std::string_view
	ClipRefusalReason(const ClipInfo& clip) noexcept
	{
		if (clip.frameCount < 2)
			return "has a single frame, so it has no cycle for a blend space to share";

		return {};
	}

	SpeedThresholds
	ThresholdsFromSpeed(
		const std::span<const assetlib::BlendSpaceSample> run,
		const std::span<const ClipInfo>                   clips)
	{
		auto taken = std::vector<assetlib::BlendSpaceSample>(run.begin(), run.end());

		for (assetlib::BlendSpaceSample& sample : taken)
		{
			const auto clip = std::ranges::find_if(clips, [&sample](const ClipInfo& c) {
				return c.name == sample.clip;
			});

			if (clip == clips.end())
				return { {}, "'" + sample.clip + "' is not a clip of this set" };

			sample.parameter = clip->locomotionSpeed;
		}

		std::ranges::sort(taken, {}, &assetlib::BlendSpaceSample::parameter);

		for (size_t i = 1; i < taken.size(); ++i)
		{
			if (taken[i].parameter > taken[i - 1].parameter)
				continue;

			return { {},
				     "'" + taken[i - 1].clip + "' and '" + taken[i].clip +
				         "' were animated at the same speed, so there is no run of increasing "
				         "thresholds to take from them" };
		}

		return { std::move(taken), {} };
	}

	const game::BlendSpaceInfo*
	SpaceForNode(
		const std::span<const game::BlendSpaceInfo> spaces,
		const size_t                                clipCount,
		const int                                   node) noexcept
	{
		if (node < 0 || static_cast<size_t>(node) < clipCount)
			return nullptr;

		const size_t index = static_cast<size_t>(node) - clipCount;
		return index < spaces.size() ? &spaces[index] : nullptr;
	}

	float
	NodeSampleRate(
		const std::span<const ClipInfo>             clips,
		const std::span<const game::BlendSpaceInfo> spaces,
		const int                                   node,
		const float                                 parameter) noexcept
	{
		if (node < 0 || clips.empty())
			return 0.0f;

		const game::BlendSpaceInfo* space = SpaceForNode(spaces, clips.size(), node);
		if (space == nullptr)
			return static_cast<size_t>(node) < clips.size() ?
			           clips[static_cast<size_t>(node)].sampleRate :
			           0.0f;

		if (space->samples.empty())
			return 0.0f;

		const uint32_t clip = space->samples[space->StraddleAt(parameter).lower].clipIndex;
		return clip < clips.size() ? clips[clip].sampleRate : 0.0f;
	}
}
