#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/PreparedStaticMesh.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
	constexpr uint32_t c_Size = 256;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;
		return opts;
	}

	bgl::SceneDesc
	TriangleSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialSubmeshes            = 4;
		desc.initialMeshlets             = 16;
		desc.initialVertexBufferByteSize = 4096;
		desc.initialIndices              = 64;
		desc.initialPbrMaterials         = 4;
		return desc;
	}

	// One submesh, one meshlet, one triangle drawn with both windings so no facing convention can
	// cull it out of the assertions below.
	assetlib::BMesh
	MakeTriangleMesh()
	{
		constexpr uint16_t c_Stride = 12;  // one float32x3 position

		auto mesh = assetlib::BMesh();
		mesh.stringPool.push_back('\0');

		const std::array<glm::vec3, 3> positions = { glm::vec3(-1.0f, -1.0f, 0.0f),
			                                         glm::vec3(1.0f, -1.0f, 0.0f),
			                                         glm::vec3(0.0f, 1.0f, 0.0f) };
		mesh.vertexData.resize(positions.size() * c_Stride);
		std::memcpy(mesh.vertexData.data(), positions.data(), mesh.vertexData.size());

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexOffset   = 0;
		meshlet.triangleOffset = 0;
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 2;
		meshlet.boundingCenter = glm::vec3(0.0f);
		meshlet.boundingRadius = 2.0f;
		mesh.meshlets.push_back(meshlet);

		for (const uint32_t v : { 0u, 1u, 2u }) mesh.meshletVertices.push_back(v);
		for (const uint32_t t : { 0u, 1u, 2u, 0u, 2u, 1u })
			mesh.meshletTriangles.push_back(static_cast<uint8_t>(t));

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 1;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.vertexByteOffset      = 0;
		submesh.vertexCount           = 3;
		submesh.firstMeshlet          = 0;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(-1.0f, -1.0f, 0.0f);
		submesh.aabbMax               = glm::vec3(1.0f, 1.0f, 0.0f);
		submesh.nameOffset            = 0;
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = 1;
		entry.nameOffset   = 0;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	bgl::RenderJob
	TriangleJob(const bgl::SceneViewRef& view)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 3.0f),
				glm::vec3(0.0f, 0.0f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
		return job;
	}

	int
	DistinctColours(const assetlib::ImageData& image)
	{
		auto seen = std::set<uint32_t>();

		const auto*  bytes    = reinterpret_cast<const uint8_t*>(image.pixels.data());
		const size_t rowPitch = image.subresources.front().rowPitch;

		for (uint32_t y = 0; y < image.height && seen.size() < 8; ++y)
		{
			for (uint32_t x = 0; x < image.width && seen.size() < 8; ++x)
			{
				uint32_t pixel = 0;
				std::memcpy(&pixel, bytes + y * rowPitch + x * 4u, 4u);
				seen.insert(pixel);
			}
		}

		return static_cast<int>(seen.size());
	}
}

// The split must be invisible in the output: cooking on a worker and committing on the driving
// thread has to produce the very pixels the fused path draws, or the two code paths have diverged.
TEST_CASE("A mesh cooked on a worker draws what the direct path draws", "[scene][cook]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto rtDesc     = bgl::RenderTargetDesc();
	rtDesc.width    = static_cast<int>(c_Size);
	rtDesc.height   = static_cast<int>(c_Size);
	rtDesc.headless = true;
	auto target     = gfx->CreateRenderTarget(rtDesc);

	auto scene = gfx->CreateScene(TriangleSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	// Unlit PBR is black on a black clear, which would blank the whole comparison.
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const assetlib::BMesh mesh = MakeTriangleMesh();

	const std::array<bgl::MaterialHandle, 1> materials = { scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f) }) };

	const auto shoot = [&](bgl::GeomHandle geom) {
		const bgl::MeshInstanceHandle instance =
			view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

		// Two frames so the presented backbuffer holds a fully uploaded scene.
		gfx->DrawFrame(target, TriangleJob(view));
		gfx->DrawFrame(target, TriangleJob(view));
		assetlib::ImageData image = gfx->ScreenshotToMemory(target);

		view->DeleteMeshInstance(instance);
		scene->DeleteGeom(geom);
		return image;
	};

	const assetlib::ImageData direct = shoot(scene->AddStaticMesh(mesh, 0, materials));

	// The cook must not need the driving thread; this is the one bgl call allowed off it.
	auto prepared = bgl::PreparedStaticMesh();
	auto worker   = std::thread([&] { prepared = bgl::CookStaticMesh(mesh, 0); });
	worker.join();

	const assetlib::ImageData cooked = shoot(scene->AddStaticMesh(std::move(prepared), materials));

	// The triangle actually drew -- a culled or empty commit would make the comparison vacuous.
	REQUIRE(DistinctColours(direct) > 1);

	REQUIRE(direct.pixels.size() == cooked.pixels.size());
	CHECK(std::memcmp(direct.pixels.data(), cooked.pixels.data(), direct.pixels.size()) == 0);
}

TEST_CASE("A prepared mesh is single-spend", "[scene][cook]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(TriangleSceneDesc());

	const assetlib::BMesh mesh = MakeTriangleMesh();

	auto prepared = bgl::CookStaticMesh(mesh, 0);
	scene->DeleteGeom(scene->AddStaticMesh(std::move(prepared), {}));

	// Moved-from: the commit consumed it, so a second commit has nothing to upload.
	CHECK_THROWS_AS(scene->AddStaticMesh(std::move(prepared), {}), bgl::SceneError);

	CHECK_THROWS_AS(bgl::CookStaticMesh(mesh, 1), bgl::SceneError);
}
