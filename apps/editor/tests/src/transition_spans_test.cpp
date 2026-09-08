#include "Windows/AnimationEditor/TransitionStrip.h"
#include "Windows/AnimationEditor/transition_spans.h"

#include <catch2/catch_test_macros.hpp>

// The strip's whole picture is these four spans, and each one stands for something the record
// actually says: the outgoing clip ends where its ramp reaches zero, the incoming one begins where
// its slot is given tRef, and the overlap between them is the duration itself. A picture that
// disagreed with the record would be worse than no picture, since the point is judging by eye.

namespace
{
	using editor::SpansForTransition;
	using editor::TransitionLayout;
	using editor::WindowFor;

	// A 0.5 s fade at t = 1, with 1 s of lead and 1 s of tail: the window is [0, 2.5] and 250 px
	// wide, so one second is 100 px and the fade is 50.
	TransitionLayout
	Fade()
	{
		auto layout        = TransitionLayout();
		layout.windowStart = 0.0f;
		layout.windowEnd   = 2.5f;
		layout.start       = 1.0f;
		layout.duration    = 0.5f;
		layout.time        = 0.0f;
		return layout;
	}
}

TEST_CASE("The overlap is the duration, and the bars meet across it", "[animation]")
{
	const auto spans = SpansForTransition(Fade(), 250);

	// The outgoing clip is already playing when the window opens and holds until its ramp is done.
	CHECK(spans.from.x == 0);
	CHECK(spans.from.width == 150);

	// The incoming one starts where the fade does and runs to the end of the window.
	CHECK(spans.to.x == 100);
	CHECK(spans.to.width == 150);

	// And what they share is the fade: 0.5 s of a 2.5 s window across 250 px.
	CHECK(spans.overlap.x == 100);
	CHECK(spans.overlap.width == 50);

	// The overlap is exactly where the two bars cross, which is the claim the widget rests on.
	CHECK(spans.overlap.x == spans.to.x);
	CHECK(spans.overlap.x + spans.overlap.width == spans.from.x + spans.from.width);
}

TEST_CASE("The playhead tracks the clock across the window", "[animation]")
{
	auto layout = Fade();

	layout.time = 0.0f;
	CHECK(SpansForTransition(layout, 250).playheadX == 0);

	layout.time = 1.25f;
	CHECK(SpansForTransition(layout, 250).playheadX == 125);

	layout.time = 2.5f;
	CHECK(SpansForTransition(layout, 250).playheadX == 250);

	// A clock outside the window is clamped rather than drawn off the end -- the transport clamps
	// there too, so this only ever shows during a resize.
	layout.time = -10.0f;
	CHECK(SpansForTransition(layout, 250).playheadX == 0);
	layout.time = 99.0f;
	CHECK(SpansForTransition(layout, 250).playheadX == 250);
}

TEST_CASE("An instant fade draws an empty overlap, not an inverted one", "[animation]")
{
	auto layout     = Fade();
	layout.duration = 0.0f;

	const auto spans = SpansForTransition(layout, 250);

	CHECK(spans.overlap.width == 0);
	CHECK(spans.from.width == 100);
	CHECK(spans.to.x == 100);
}

TEST_CASE("A degenerate window or width draws nothing at all", "[animation]")
{
	auto layout = Fade();

	CHECK(SpansForTransition(layout, 0).from.width == 0);
	CHECK(SpansForTransition(layout, -5).to.width == 0);

	layout.windowEnd = layout.windowStart;
	const auto flat  = SpansForTransition(layout, 250);
	CHECK(flat.from.width == 0);
	CHECK(flat.to.width == 0);
	CHECK(flat.overlap.width == 0);
	CHECK(flat.playheadX == 0);
}

TEST_CASE("A window brackets the fade with lead and tail", "[animation]")
{
	const auto layout =
		WindowFor(/*start*/ 10.0f, /*duration*/ 0.25f, /*lead*/ 0.5f, /*tail*/ 1.0f);

	CHECK(layout.windowStart == 9.5f);
	CHECK(layout.windowEnd == 11.25f);
	CHECK(layout.start == 10.0f);
	CHECK(layout.duration == 0.25f);

	// Parked at the start, so playing it once runs the whole fade.
	CHECK(layout.time == layout.windowStart);
}

TEST_CASE("A window is never empty, whatever it is asked for", "[animation]")
{
	// PlaybackTransport refuses an empty window outright, so this is what stops a duration of zero
	// with no lead and no tail from reaching it.
	const auto degenerate = WindowFor(5.0f, 0.0f, 0.0f, 0.0f);
	CHECK(degenerate.windowEnd > degenerate.windowStart);

	// Negative lead, tail and duration are floors rather than refusals: the strip's controls cannot
	// produce one, and a picture is not worth an exception.
	const auto negative = WindowFor(5.0f, -1.0f, -1.0f, -1.0f);
	CHECK(negative.windowEnd > negative.windowStart);
	CHECK(negative.duration == 0.0f);
}

// The strip's other pure half: where a click lands on the clock. Its inverse is SpansForTransition's
// playhead, and a drag that did not land back on the pixel it was made at would read as a jump.

TEST_CASE("A click maps to the clock it points at, and back", "[animation]")
{
	const auto layout = Fade();

	CHECK(TransitionStrip::TimeForX(layout, 0, 250) == 0.0f);
	CHECK(TransitionStrip::TimeForX(layout, 125, 250) == 1.25f);
	CHECK(TransitionStrip::TimeForX(layout, 250, 250) == 2.5f);

	// Outside the strip clamps into the window, which is where a drag leaving the widget ends up.
	CHECK(TransitionStrip::TimeForX(layout, -40, 250) == 0.0f);
	CHECK(TransitionStrip::TimeForX(layout, 400, 250) == 2.5f);

	// Round-trips against the span map, so the playhead lands under the cursor rather than beside it.
	auto scrubbed = layout;
	scrubbed.time = TransitionStrip::TimeForX(layout, 175, 250);
	CHECK(SpansForTransition(scrubbed, 250).playheadX == 175);
}

TEST_CASE(
	"A degenerate strip seeks to the window start rather than dividing by nothing",
	"[animation]")
{
	auto layout = Fade();
	CHECK(TransitionStrip::TimeForX(layout, 100, 0) == layout.windowStart);

	layout.windowEnd = layout.windowStart;
	CHECK(TransitionStrip::TimeForX(layout, 100, 250) == layout.windowStart);
}
