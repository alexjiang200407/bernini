#include <bgl_common/PassScheduler.h>

using namespace bgl;

namespace
{
	PassScheduler::Access
	Write(std::string name)
	{
		return { std::move(name), true };
	}

	PassScheduler::Access
	Read(std::string name)
	{
		return { std::move(name), false };
	}

	PassScheduler::Pass
	Pass(std::vector<PassScheduler::Access> accesses, bool pinned = false)
	{
		return { pinned, std::move(accesses) };
	}
}

TEST_CASE("PassScheduler: culls a pass whose outputs are never used", "[scheduler]")
{
	PassScheduler sched;
	const size_t  main   = sched.AddPass(Pass({ Write("backbuffer") }, /*pinned=*/true));
	const size_t  unused = sched.AddPass(Pass({ Write("scratch") }));

	sched.Compile();

	CHECK(sched.Order() == std::vector<size_t>{ main });
	CHECK(sched.WasCulled(unused));
	CHECK_FALSE(sched.WasCulled(main));
}

TEST_CASE("PassScheduler: a consumed producer survives culling", "[scheduler]")
{
	PassScheduler sched;
	const size_t  produce = sched.AddPass(Pass({ Write("gbuffer") }));
	const size_t  consume =
		sched.AddPass(Pass({ Read("gbuffer"), Write("backbuffer") }, /*pinned=*/true));

	sched.Compile();

	CHECK(sched.Order() == std::vector<size_t>{ produce, consume });
}

TEST_CASE("PassScheduler: transitively culls a dead producer chain", "[scheduler]")
{
	PassScheduler sched;
	sched.AddPass(Pass({ Write("t1") }));
	sched.AddPass(Pass({ Read("t1"), Write("t2") }));

	sched.Compile();

	CHECK(sched.Order().empty());
	CHECK(sched.WasCulled(0));
	CHECK(sched.WasCulled(1));
}

TEST_CASE("PassScheduler: a pinned pass survives with no consumer", "[scheduler]")
{
	PassScheduler sched;
	const size_t  debug = sched.AddPass(Pass({ Write("scratch") }, /*pinned=*/true));

	sched.Compile();

	CHECK(sched.Order() == std::vector<size_t>{ debug });
	CHECK_FALSE(sched.WasCulled(debug));
}

// The edge is taken for every access, a write included, so a write-after-write keeps the earlier
// writer alive: the graph cannot know the second pass overwrites the whole resource rather than
// accumulating into what the first left, and culling it would be culling a live producer.
TEST_CASE("PassScheduler: a write-after-write keeps the earlier writer", "[scheduler]")
{
	PassScheduler sched;
	const size_t  first  = sched.AddPass(Pass({ Write("t") }));
	const size_t  second = sched.AddPass(Pass({ Write("t") }));
	const size_t  reader = sched.AddPass(Pass({ Read("t") }, /*pinned=*/true));

	sched.Compile();

	CHECK(sched.Order() == std::vector<size_t>{ first, second, reader });
}

// Names arrive resolved, so two that differ are two resources however alike they look: no edge
// between them, and the producer culls as dead. This is what keeps a transient produced under one
// scope from being reachable under another.
TEST_CASE("PassScheduler: names that resolved differently are different resources", "[scheduler]")
{
	PassScheduler sched;
	sched.AddPass(Pass({ Write("v0:tmp") }));
	const size_t consumer = sched.AddPass(Pass({ Read("v1:tmp") }, /*pinned=*/true));

	sched.Compile();

	CHECK(sched.Order() == std::vector<size_t>{ consumer });
	CHECK(sched.WasCulled(0));
}

TEST_CASE("PassScheduler: reading a resource is not on its own a root", "[scheduler]")
{
	PassScheduler sched;
	const size_t  reader = sched.AddPass(Pass({ Read("input") }));

	sched.Compile();

	CHECK(sched.WasCulled(reader));
	CHECK(sched.Order().empty());
}

TEST_CASE("PassScheduler: Compile is idempotent", "[scheduler]")
{
	PassScheduler sched;
	const size_t  keep = sched.AddPass(Pass({ Write("out") }, /*pinned=*/true));
	sched.AddPass(Pass({ Write("dead") }));

	sched.Compile();
	const std::vector<size_t> first = sched.Order();
	sched.Compile();

	CHECK(first == std::vector<size_t>{ keep });
	CHECK(sched.Order() == first);
}

TEST_CASE("PassScheduler: Clear drops the passes and the order", "[scheduler]")
{
	PassScheduler sched;
	sched.AddPass(Pass({ Write("out") }, /*pinned=*/true));
	sched.Compile();
	REQUIRE(sched.Count() == 1);

	sched.Clear();

	CHECK(sched.Count() == 0);
	CHECK(sched.Order().empty());
}
