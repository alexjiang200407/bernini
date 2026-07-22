#include <catch2/catch_approx.hpp>
#include <core/stats/RollingWindow.h>

TEST_CASE("RollingWindow reports zero statistics while empty", "[rollingwindow]")
{
	core::RollingWindow<4> window;

	REQUIRE(window.Size() == 0u);
	REQUIRE(window.Capacity() == 4u);
	REQUIRE(window.Mean() == 0.0);
	REQUIRE(window.Max() == 0.0);
}

TEST_CASE("RollingWindow averages only the samples it holds", "[rollingwindow]")
{
	core::RollingWindow<4> window;

	window.Push(10.0);
	window.Push(20.0);

	// Not 7.5 -- the two unfilled slots must not count toward the mean.
	REQUIRE(window.Size() == 2u);
	REQUIRE(window.Mean() == Catch::Approx(15.0));
	REQUIRE(window.Max() == Catch::Approx(20.0));
}

TEST_CASE("RollingWindow overwrites the oldest sample once full", "[rollingwindow]")
{
	core::RollingWindow<3> window;

	window.Push(1.0);
	window.Push(2.0);
	window.Push(3.0);
	REQUIRE(window.Mean() == Catch::Approx(2.0));

	window.Push(4.0);

	// 1.0 has aged out; the window is {2,3,4}.
	REQUIRE(window.Size() == 3u);
	REQUIRE(window.Mean() == Catch::Approx(3.0));
}

TEST_CASE("RollingWindow forgets a spike once it ages out", "[rollingwindow]")
{
	core::RollingWindow<2> window;

	window.Push(1.0);
	window.Push(700.0);
	REQUIRE(window.Max() == Catch::Approx(700.0));

	window.Push(1.0);
	window.Push(1.0);

	// The whole point of a rolling max: a past stall must stop being reported as current.
	REQUIRE(window.Max() == Catch::Approx(1.0));
}

TEST_CASE("RollingWindow reset empties it without disturbing capacity", "[rollingwindow]")
{
	core::RollingWindow<4> window;

	window.Push(5.0);
	window.Push(6.0);
	window.Reset();

	REQUIRE(window.Size() == 0u);
	REQUIRE(window.Mean() == 0.0);
	REQUIRE(window.Max() == 0.0);
	REQUIRE(window.Capacity() == 4u);

	window.Push(9.0);
	REQUIRE(window.Mean() == Catch::Approx(9.0));
}

TEST_CASE("RollingWindow of one always reports the newest sample", "[rollingwindow]")
{
	core::RollingWindow<1> window;

	window.Push(3.0);
	REQUIRE(window.Mean() == Catch::Approx(3.0));

	window.Push(8.0);
	REQUIRE(window.Size() == 1u);
	REQUIRE(window.Mean() == Catch::Approx(8.0));
	REQUIRE(window.Max() == Catch::Approx(8.0));
}
