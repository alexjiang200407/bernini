#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The meshlet accounting the expansion chain sizes its record buffer from: a view knows the total
// meshlets across its instances, kept in step as instances come and go. This is also the first time
// Scene and SceneView are constructed on the WebGPU backend at all -- the samplers and buffers their
// constructors need only recently stopped gfataling -- so the fixture existing is itself part of
// what is under test.

namespace
{
	struct Fixture
	{
		core::SharedRef<Device> device  = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
		ResourceManagerRef      manager = device->CreateResourceManager(ResourceManagerDesc{});
		CommandQueueRef         queue   = device->CreateCommandQueue(QueueType::kGraphics);

		core::SharedRef<Scene> scene = core::SharedRef<Scene>::Make(SceneDesc{}, manager);

		Fixture() { manager->RegisterQueue(queue.Get()); }

		~Fixture()
		{
			queue->Flush();
			manager->UnregisterQueue(queue.Get());
		}
	};
}

TEST_CASE("A view's meshlet total tracks its instances", "[wgpu][scene]")
{
	auto fixture = Fixture{};

	auto view = core::SharedRef<SceneView>::Make(
		core::SharedRef<IScene>(fixture.scene),
		8u,
		fixture.manager);

	CHECK(view->GetMeshletCount() == 0);

	const auto cube = fixture.scene->AddCubeGeom();

	const auto a = view->CreateStaticMeshInstance(cube, glm::mat4(1.0f));

	// A cube is one submesh with at least one meshlet; the exact count is the meshletizer's
	// business, but it must be positive and stable.
	const uint32_t perCube = view->GetMeshletCount();
	REQUIRE(perCube > 0);

	const auto b = view->CreateStaticMeshInstance(cube, glm::mat4(1.0f));
	CHECK(view->GetMeshletCount() == 2 * perCube);

	view->DeleteMeshInstance(a);
	CHECK(view->GetMeshletCount() == perCube);

	view->DeleteMeshInstance(b);
	CHECK(view->GetMeshletCount() == 0);
}

TEST_CASE("Two geoms sum their meshlets through one view", "[wgpu][scene]")
{
	auto fixture = Fixture{};

	auto view = core::SharedRef<SceneView>::Make(
		core::SharedRef<IScene>(fixture.scene),
		8u,
		fixture.manager);

	const auto cube   = fixture.scene->AddCubeGeom();
	const auto sphere = fixture.scene->AddSphereGeom(16, 16, 1.0f);

	view->CreateStaticMeshInstance(cube, glm::mat4(1.0f));
	const uint32_t cubeOnly = view->GetMeshletCount();

	view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));
	const uint32_t both = view->GetMeshletCount();

	// A 16x16 sphere carries more geometry than a cube; the total must reflect both, not either.
	CHECK(both > cubeOnly);
	CHECK(cubeOnly > 0);
}
