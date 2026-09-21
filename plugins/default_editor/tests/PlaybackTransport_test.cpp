#include "Windows/AnimationEditor/PlaybackTransport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <vector>

// The transport is the whole clock behind the Animation panel -- the preview instance is always
// {clip, phase 0, rate 1} -- so these cases pin its arithmetic frame-exactly against the shader's
// ClipFrames: a loop wraps at frameCount frames, a one-shot clamps to its last.

namespace
{
	using editor::ClipInfo;
	using editor::PlaybackTransport;

	// 4 frames at 10 Hz, so 3 intervals: both kinds end at frame 3 = 0.3 s, a loop wrapping there
	// and a one-shot stopping. The recorded 0.3 s is the importer's own duration for such a clip
	// ((frameCount - 1) / sampleRate), which is the same arithmetic from the other side.
	std::vector<ClipInfo>
	OneClip(bool loop)
	{
		return { { "walk", 4, 10.0f, 0.3f, loop } };
	}

	PlaybackTransport
	Loaded(bool loop)
	{
		auto transport = PlaybackTransport();
		transport.SetClips(OneClip(loop));
		return transport;
	}
}

TEST_CASE("An empty transport is inert", "[animation]")
{
	auto transport = PlaybackTransport();

	CHECK_FALSE(transport.HasClips());
	CHECK(transport.GetTimeSeconds() == 0.0f);
	CHECK_FALSE(transport.GetCurrentFrame().has_value());
	CHECK(transport.GetPeriodSeconds() == 0.0f);

	transport.Play();
	transport.Advance(1.0f);
	transport.Scrub(0.5f);
	transport.StepFrames(1);

	CHECK_FALSE(transport.IsPlaying());
	CHECK(transport.GetTimeSeconds() == 0.0f);
	CHECK_THROWS_AS(transport.GetActiveClip(), std::runtime_error);
}

TEST_CASE("SetClips selects clip 0, rewound and paused", "[animation]")
{
	auto transport = Loaded(true);

	CHECK(transport.HasClips());
	CHECK(transport.GetActiveClipIndex() == 0);
	CHECK(transport.GetTimeSeconds() == 0.0f);
	CHECK_FALSE(transport.IsPlaying());
	CHECK(transport.GetActiveClip().name == "walk");

	CHECK_THROWS_AS(transport.SelectClip(1), std::runtime_error);
}

TEST_CASE("A clip with no frames or a dead sample rate is refused up front", "[animation]")
{
	auto transport = PlaybackTransport();

	CHECK_THROWS_AS(transport.SetClips({ { "empty", 0, 10.0f, 0.0f, true } }), std::runtime_error);
	CHECK_THROWS_AS(transport.SetClips({ { "frozen", 4, 0.0f, 0.0f, true } }), std::runtime_error);
	CHECK_FALSE(transport.HasClips());
}

TEST_CASE("SelectClip rewinds but keeps play state and speed", "[animation]")
{
	auto transport = PlaybackTransport();
	transport.SetClips({ { "walk", 4, 10.0f, 0.3f, true }, { "run", 8, 30.0f, 0.2333f, true } });

	transport.Play();
	transport.SetSpeed(0.5f);
	transport.Advance(0.2f);
	REQUIRE(transport.GetTimeSeconds() == Catch::Approx(0.1f));

	transport.SelectClip(1);

	CHECK(transport.GetActiveClipIndex() == 1);
	CHECK(transport.GetTimeSeconds() == 0.0f);
	CHECK(transport.IsPlaying());
	CHECK(transport.GetSpeed() == 0.5f);
	CHECK(transport.GetActiveClip().name == "run");
}

TEST_CASE("A looping clip wraps at frameCount - 1 frames, as the shader does", "[animation]")
{
	auto transport = Loaded(true);
	transport.Play();

	// The clip's own recorded duration, which is what a loop's period must be: its last frame is a
	// repeat of its first, so the cycle ends there rather than one interval later. A transport that
	// wrapped a step late would put the playhead somewhere the GPU is not.
	CHECK(transport.GetPeriodSeconds() == Catch::Approx(0.3f));
	CHECK(transport.GetPeriodSeconds() == Catch::Approx(transport.GetActiveClip().duration));

	transport.Advance(0.45f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.15f));
	CHECK(transport.GetCurrentFrame() == Catch::Approx(1.5f));

	// Backwards across zero lands inside the period from the top.
	transport.SetSpeed(-1.0f);
	transport.Advance(0.25f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.2f));
	CHECK(transport.GetCurrentFrame() == Catch::Approx(2.0f));
}

TEST_CASE("A one-shot clamps to its last frame and rewinds on Play from the end", "[animation]")
{
	auto transport = Loaded(false);
	transport.Play();

	CHECK(transport.GetPeriodSeconds() == Catch::Approx(0.3f));

	transport.Advance(1.0f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.3f));
	CHECK(transport.GetCurrentFrame() == Catch::Approx(3.0f));
	CHECK(transport.IsPlaying());

	// Parked on the end, Play starts over rather than replaying one held frame.
	transport.Play();
	CHECK(transport.GetTimeSeconds() == 0.0f);

	// Backwards from zero stays parked at zero.
	transport.SetSpeed(-1.0f);
	transport.Advance(0.2f);
	CHECK(transport.GetTimeSeconds() == 0.0f);
}

TEST_CASE("Speed scales wall time; a pause holds the clock", "[animation]")
{
	auto transport = Loaded(true);
	transport.Play();
	transport.SetSpeed(0.25f);

	transport.Advance(0.4f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.1f));

	transport.Pause();
	transport.Advance(1.0f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.1f));
}

TEST_CASE("Scrub parks the clock inside the clip's domain", "[animation]")
{
	auto looped = Loaded(true);
	looped.Scrub(0.45f);
	CHECK(looped.GetTimeSeconds() == Catch::Approx(0.15f));

	auto oneShot = Loaded(false);
	oneShot.Scrub(0.5f);
	CHECK(oneShot.GetTimeSeconds() == Catch::Approx(0.3f));
	oneShot.Scrub(-0.1f);
	CHECK(oneShot.GetTimeSeconds() == 0.0f);

	oneShot.Play();
	oneShot.Scrub(0.1f);
	CHECK(oneShot.IsPlaying());
}

TEST_CASE("StepFrames pauses and moves whole frames", "[animation]")
{
	auto transport = Loaded(true);
	transport.Play();
	transport.Scrub(0.14f);  // frame 1.4

	transport.StepFrames(1);
	CHECK_FALSE(transport.IsPlaying());
	CHECK(transport.GetCurrentFrame() == Catch::Approx(2.0f));
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.2f));

	// A loop wraps in both directions. Frame 2 is the last distinct one -- frame 3 repeats frame 0 --
	// so stepping off it lands on 0, and stepping back off 0 lands on 2.
	transport.Scrub(0.2f);
	transport.StepFrames(1);
	CHECK(transport.GetCurrentFrame() == Catch::Approx(0.0f));
	transport.StepFrames(-1);
	CHECK(transport.GetCurrentFrame() == Catch::Approx(2.0f));

	// A one-shot clamps at both ends instead.
	auto oneShot = Loaded(false);
	oneShot.StepFrames(-1);
	CHECK(oneShot.GetCurrentFrame() == Catch::Approx(0.0f));
	oneShot.Scrub(0.3f);
	oneShot.StepFrames(5);
	CHECK(oneShot.GetCurrentFrame() == Catch::Approx(3.0f));
}

// The second domain. A crossfade's ramp is stamped in absolute time, so the window that brackets it
// must not wrap -- these pin that it clamps at both ends whatever the active clip does, that the
// clip's own domain is untouched underneath, and that the timeline reads the same 0..1 in both.

TEST_CASE("A transition window is absolute, clamped at both ends, and never wraps", "[animation]")
{
	auto transport = Loaded(/*loop*/ true);
	transport.SetTransitionWindow(10.0f, 11.0f);

	CHECK(transport.InTransitionWindow());
	CHECK(transport.GetTimeSeconds() == 10.0f);
	CHECK(transport.GetWindowStartSeconds() == 10.0f);
	CHECK(transport.GetWindowEndSeconds() == 11.0f);

	// The clip loops and its period is 0.3 s, so a clip-local clock would have wrapped this many
	// times over. The window is the domain, and it holds.
	transport.Play();
	transport.Advance(0.4f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(10.4f));

	transport.Advance(5.0f);
	CHECK(transport.GetTimeSeconds() == 11.0f);

	transport.Scrub(9.0f);
	CHECK(transport.GetTimeSeconds() == 10.0f);

	transport.Scrub(10.25f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(10.25f));
}

TEST_CASE("An empty or reversed window is refused up front", "[animation]")
{
	auto transport = Loaded(/*loop*/ true);

	CHECK_THROWS_AS(transport.SetTransitionWindow(1.0f, 1.0f), std::runtime_error);
	CHECK_THROWS_AS(transport.SetTransitionWindow(2.0f, 1.0f), std::runtime_error);
	CHECK_FALSE(transport.InTransitionWindow());
}

TEST_CASE("Play rewinds a window parked on its end", "[animation]")
{
	auto transport = Loaded(/*loop*/ true);
	transport.SetTransitionWindow(10.0f, 11.0f);

	transport.Scrub(11.0f);
	transport.Play();
	CHECK(transport.GetTimeSeconds() == 10.0f);
	CHECK(transport.IsPlaying());
}

TEST_CASE("A window has no frame of its own, and steps in the clip's interval", "[animation]")
{
	auto transport = Loaded(/*loop*/ true);
	transport.SetTransitionWindow(10.0f, 11.0f);

	// Two slots are live over a fade and neither one's frame is the playhead. Empty rather than
	// zero, which is a frame the clip really has.
	CHECK_FALSE(transport.GetCurrentFrame().has_value());

	// 10 Hz, so a frame is 0.1 s of the window's seconds.
	transport.StepFrames(3);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(10.3f));
	CHECK_FALSE(transport.IsPlaying());

	transport.StepFrames(-1);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(10.2f));

	transport.StepFrames(100);
	CHECK(transport.GetTimeSeconds() == 11.0f);
}

TEST_CASE("Leaving a window restores the clip's own domain", "[animation]")
{
	auto transport = Loaded(/*loop*/ true);
	transport.SetTransitionWindow(10.0f, 11.0f);
	transport.Scrub(10.5f);

	transport.ClearTransitionWindow();

	CHECK_FALSE(transport.InTransitionWindow());
	CHECK(transport.GetTimeSeconds() == 0.0f);

	// The clip's wrap is exactly what it was before the window existed.
	transport.Scrub(0.35f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.05f));
	CHECK(transport.GetCurrentFrame() == Catch::Approx(0.5f));
}

TEST_CASE("A clip selection or a new clip table drops the window", "[animation]")
{
	auto transport = Loaded(/*loop*/ true);

	transport.SetTransitionWindow(10.0f, 11.0f);
	transport.SelectClip(0);
	CHECK_FALSE(transport.InTransitionWindow());
	CHECK(transport.GetTimeSeconds() == 0.0f);

	transport.SetTransitionWindow(10.0f, 11.0f);
	transport.SetClips(OneClip(/*loop*/ false));
	CHECK_FALSE(transport.InTransitionWindow());
	CHECK(transport.GetTimeSeconds() == 0.0f);
}
