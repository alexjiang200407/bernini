#include "Windows/AnimationEditor/PlaybackTransport.h"

#include <catch2/catch_approx.hpp>

// The transport is the whole clock behind the Animation panel -- the preview instance is always
// {clip, phase 0, rate 1} -- so these cases pin its arithmetic frame-exactly against the shader's
// ClipFrames: a loop wraps at frameCount frames, a one-shot clamps to its last.

namespace
{
	using editor::ClipInfo;
	using editor::PlaybackTransport;

	// 4 frames at 10 Hz: a loop wraps at 0.4 s, a one-shot ends at frame 3 = 0.3 s.
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
	CHECK(transport.GetCurrentFrame() == 0.0f);
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

TEST_CASE("A looping clip wraps at frameCount frames, as the shader does", "[animation]")
{
	auto transport = Loaded(true);
	transport.Play();

	CHECK(transport.GetPeriodSeconds() == Catch::Approx(0.4f));

	transport.Advance(0.45f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.05f));
	CHECK(transport.GetCurrentFrame() == Catch::Approx(0.5f));

	// Backwards across zero lands inside the period from the top.
	transport.SetSpeed(-1.0f);
	transport.Advance(0.15f);
	CHECK(transport.GetTimeSeconds() == Catch::Approx(0.3f));
	CHECK(transport.GetCurrentFrame() == Catch::Approx(3.0f));
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
	CHECK(looped.GetTimeSeconds() == Catch::Approx(0.05f));

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

	// A loop wraps in both directions; frame 3 + 1 is frame 0.
	transport.Scrub(0.3f);
	transport.StepFrames(1);
	CHECK(transport.GetCurrentFrame() == Catch::Approx(0.0f));
	transport.StepFrames(-1);
	CHECK(transport.GetCurrentFrame() == Catch::Approx(3.0f));

	// A one-shot clamps at both ends instead.
	auto oneShot = Loaded(false);
	oneShot.StepFrames(-1);
	CHECK(oneShot.GetCurrentFrame() == Catch::Approx(0.0f));
	oneShot.Scrub(0.3f);
	oneShot.StepFrames(5);
	CHECK(oneShot.GetCurrentFrame() == Catch::Approx(3.0f));
}
