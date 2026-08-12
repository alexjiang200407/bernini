#include "Windows/MaterialEditor/MaterialPreviewWindow.h"

#include <catch2/catch_test_macros.hpp>

// The selector-index -> selection mapping behind SetSelectedSubmesh and SetSubmeshMaterial, pinned
// without a window or a device: which (instance, submesh) pairs a selector entry acts on. The
// window itself only walks this list on the render thread.

namespace
{
	bgl::MeshInstanceHandle
	Handle(uint32_t index)
	{
		auto handle         = bgl::MeshInstanceHandle();
		handle.handle.index = index;
		return handle;
	}

	using SubmeshRef  = MaterialPreviewWindow::SubmeshRef;
	using InstanceRef = MaterialPreviewWindow::InstanceRef;
}

TEST_CASE("Selecting a submesh targets every instance of its geom", "[materialeditor][selection]")
{
	// Two geoms; geom 0 is placed twice (a mesh instanced by two nodes), geom 1 once.
	const std::vector<SubmeshRef> refs = {
		{ .geomIndex = 0, .localSubmesh = 0 },
		{ .geomIndex = 0, .localSubmesh = 1 },
		{ .geomIndex = 1, .localSubmesh = 0 },
	};
	const std::vector<InstanceRef> instances = {
		{ .handle = Handle(10), .geomIndex = 0 },
		{ .handle = Handle(11), .geomIndex = 0 },
		{ .handle = Handle(12), .geomIndex = 1 },
	};

	SECTION("A submesh of the twice-placed geom fans out to both instances")
	{
		const auto targets = MaterialPreviewWindow::GetInstanceTargets(refs, instances, 1);
		REQUIRE(targets.size() == 2);
		CHECK(targets[0].instance.handle.index == 10);
		CHECK(targets[1].instance.handle.index == 11);
		CHECK(targets[0].submeshIndex == 1);
		CHECK(targets[1].submeshIndex == 1);
	}

	SECTION("A submesh of the once-placed geom targets it alone")
	{
		const auto targets = MaterialPreviewWindow::GetInstanceTargets(refs, instances, 2);
		REQUIRE(targets.size() == 1);
		CHECK(targets[0].instance.handle.index == 12);
		CHECK(targets[0].submeshIndex == 0);
	}

	SECTION("The local submesh index is the ref's, not the selector's")
	{
		// Selector entry 2 is geom 1's first submesh: the flat selector index must not leak
		// through to the per-instance call.
		const auto targets = MaterialPreviewWindow::GetInstanceTargets(refs, instances, 2);
		REQUIRE_FALSE(targets.empty());
		CHECK(targets[0].submeshIndex != 2);
	}
}

TEST_CASE("Selection targets skip what cannot be selected", "[materialeditor][selection]")
{
	const std::vector<SubmeshRef> refs = {
		{ .geomIndex = 0, .localSubmesh = 0 },
	};

	SECTION("An out-of-range selector index -- nothing selected -- targets nothing")
	{
		const std::vector<InstanceRef> instances = { { .handle = Handle(10), .geomIndex = 0 } };
		CHECK(MaterialPreviewWindow::GetInstanceTargets(refs, instances, 1).empty());
		CHECK(MaterialPreviewWindow::GetInstanceTargets(refs, instances, 0xFFFFFFFFu).empty());
	}

	SECTION("A dead instance handle is skipped rather than passed to the view")
	{
		const std::vector<InstanceRef> instances = {
			{ .handle = bgl::MeshInstanceHandle(), .geomIndex = 0 },
			{ .handle = Handle(11), .geomIndex = 0 },
		};

		const auto targets = MaterialPreviewWindow::GetInstanceTargets(refs, instances, 0);
		REQUIRE(targets.size() == 1);
		CHECK(targets[0].instance.handle.index == 11);
	}
}

TEST_CASE(
	"A pick lands back on the selector entry that fans out to it",
	"[materialeditor][selection]")
{
	// The inverse of GetInstanceTargets: what a raycast hit -- (geom, local submesh), via the hit
	// instance -- must map to so the combo box, the graph swap and the outline all agree.
	const std::vector<SubmeshRef> refs = {
		{ .geomIndex = 0, .localSubmesh = 0 },
		{ .geomIndex = 0, .localSubmesh = 1 },
		{ .geomIndex = 1, .localSubmesh = 0 },
	};

	SECTION("Each (geom, submesh) pair finds its own selector entry")
	{
		CHECK(MaterialPreviewWindow::SelectorIndexOf(refs, 0, 0) == 0);
		CHECK(MaterialPreviewWindow::SelectorIndexOf(refs, 0, 1) == 1);
		CHECK(MaterialPreviewWindow::SelectorIndexOf(refs, 1, 0) == 2);
	}

	SECTION("A hit nothing tracks -- a stale or foreign pair -- selects nothing")
	{
		CHECK(MaterialPreviewWindow::SelectorIndexOf(refs, 2, 0) == -1);
		CHECK(MaterialPreviewWindow::SelectorIndexOf(refs, 0, 2) == -1);
	}

	SECTION("The round trip through the fan-out returns home")
	{
		const std::vector<InstanceRef> instances = {
			{ .handle = Handle(10), .geomIndex = 0 },
			{ .handle = Handle(11), .geomIndex = 1 },
		};

		for (uint32_t selector = 0; selector < refs.size(); ++selector)
		{
			for (const auto& target :
			     MaterialPreviewWindow::GetInstanceTargets(refs, instances, selector))
			{
				const auto instance =
					std::find_if(instances.begin(), instances.end(), [&](const InstanceRef& ref) {
						return ref.handle.handle.index == target.instance.handle.index;
					});
				REQUIRE(instance != instances.end());

				CHECK(
					MaterialPreviewWindow::SelectorIndexOf(
						refs,
						instance->geomIndex,
						target.submeshIndex) == static_cast<int>(selector));
			}
		}
	}
}
