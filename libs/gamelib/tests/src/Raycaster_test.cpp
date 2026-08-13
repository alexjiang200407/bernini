#include <assetlib_structs/BMesh.h>
#include <gamelib/Raycaster.h>

namespace
{
	constexpr float c_Tolerance = 1e-4f;

	struct TestSubmesh
	{
		std::vector<glm::vec3> positions;
		std::vector<uint8_t>   triangles;  // meshlet-local indices, 3 per triangle
	};

	// A one-mesh BMesh whose submeshes each hold one meshlet with an identity vertex map -- the
	// same streams the renderer draws from, which is where the Raycaster reads its triangles.
	assetlib::BMesh
	MakeMesh(std::span<const TestSubmesh> submeshes)
	{
		constexpr uint16_t c_Stride = sizeof(glm::vec3);

		auto mesh = assetlib::BMesh();
		mesh.materials.emplace_back();

		for (const TestSubmesh& src : submeshes)
		{
			auto meshlet           = assetlib::Meshlet();
			meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
			meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
			meshlet.vertexCount    = static_cast<uint32_t>(src.positions.size());
			meshlet.triangleCount  = static_cast<uint32_t>(src.triangles.size() / 3);

			for (uint32_t v = 0; v < src.positions.size(); ++v) mesh.meshletVertices.push_back(v);
			mesh.meshletTriangles.insert(
				mesh.meshletTriangles.end(),
				src.triangles.begin(),
				src.triangles.end());

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = c_Stride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = static_cast<uint32_t>(mesh.vertexData.size());
			submesh.vertexCount           = static_cast<uint32_t>(src.positions.size());
			submesh.firstMeshlet          = static_cast<uint32_t>(mesh.meshlets.size());
			submesh.meshletCount          = 1;
			submesh.aabbMin               = glm::vec3(std::numeric_limits<float>::max());
			submesh.aabbMax               = glm::vec3(std::numeric_limits<float>::lowest());
			for (const glm::vec3& p : src.positions)
			{
				submesh.aabbMin = glm::min(submesh.aabbMin, p);
				submesh.aabbMax = glm::max(submesh.aabbMax, p);
			}

			mesh.meshlets.push_back(meshlet);
			mesh.submeshes.push_back(submesh);

			const auto* bytes = reinterpret_cast<const std::byte*>(src.positions.data());
			mesh.vertexData.insert(
				mesh.vertexData.end(),
				bytes,
				bytes + src.positions.size() * c_Stride);
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = static_cast<uint32_t>(submeshes.size());
		mesh.meshes.push_back(entry);

		return mesh;
	}

	// A unit-ish triangle in the z = `depth` plane, centered on `center`.
	TestSubmesh
	TriangleAt(const glm::vec2& center, float depth)
	{
		return { { glm::vec3(center.x - 1.0f, center.y - 1.0f, depth),
			       glm::vec3(center.x + 1.0f, center.y - 1.0f, depth),
			       glm::vec3(center.x, center.y + 1.0f, depth) },
			     { 0, 1, 2 } };
	}

	game::Ray
	Forward(float x, float y)
	{
		return { glm::vec3(x, y, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) };
	}
}

TEST_CASE("The submesh in front hides the one behind it", "[gamelib][raycast]")
{
	const TestSubmesh     submeshes[] = { TriangleAt({ 0.0f, 0.0f }, 0.0f),
		                                  TriangleAt({ 0.0f, 0.0f }, 2.0f) };
	const assetlib::BMesh mesh        = MakeMesh(submeshes);

	auto caster = game::Raycaster();
	caster.AddInstance(caster.AddMesh(mesh, 0), glm::mat4(1.0f));

	const auto hit = caster.Raycast(Forward(0.0f, 0.0f));
	REQUIRE(hit.has_value());
	CHECK(hit->submeshIndex == 0);
	CHECK(std::abs(hit->t - 5.0f) < c_Tolerance);
}

TEST_CASE("Submeshes side by side each answer for their own area", "[gamelib][raycast]")
{
	const TestSubmesh     submeshes[] = { TriangleAt({ -2.0f, 0.0f }, 0.0f),
		                                  TriangleAt({ 2.0f, 0.0f }, 0.0f) };
	const assetlib::BMesh mesh        = MakeMesh(submeshes);

	auto caster = game::Raycaster();
	caster.AddInstance(caster.AddMesh(mesh, 0), glm::mat4(1.0f));

	const auto left = caster.Raycast(Forward(-2.0f, 0.0f));
	REQUIRE(left.has_value());
	CHECK(left->submeshIndex == 0);

	const auto right = caster.Raycast(Forward(2.0f, 0.0f));
	REQUIRE(right.has_value());
	CHECK(right->submeshIndex == 1);

	CHECK_FALSE(caster.Raycast(Forward(10.0f, 10.0f)).has_value());
}

TEST_CASE("The nearest of two instances takes the pick", "[gamelib][raycast]")
{
	const TestSubmesh     submeshes[] = { TriangleAt({ 0.0f, 0.0f }, 0.0f) };
	const assetlib::BMesh mesh        = MakeMesh(submeshes);

	auto caster = game::Raycaster();

	const uint32_t geometry = caster.AddMesh(mesh, 0);
	const uint32_t back =
		caster.AddInstance(geometry, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f)));
	const uint32_t front = caster.AddInstance(geometry, glm::mat4(1.0f));

	const auto hit = caster.Raycast(Forward(0.0f, 0.0f));
	REQUIRE(hit.has_value());
	CHECK(hit->instance == front);
	CHECK(hit->instance != back);
	CHECK(std::abs(hit->t - 5.0f) < c_Tolerance);
}

TEST_CASE("An instance is picked where its transform put it", "[gamelib][raycast]")
{
	const TestSubmesh     submeshes[] = { TriangleAt({ 0.0f, 0.0f }, 0.0f) };
	const assetlib::BMesh mesh        = MakeMesh(submeshes);

	auto caster = game::Raycaster();
	caster.AddInstance(
		caster.AddMesh(mesh, 0),
		glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)));

	CHECK(caster.Raycast(Forward(10.0f, 0.0f)).has_value());
	CHECK_FALSE(caster.Raycast(Forward(0.0f, 0.0f)).has_value());
}

TEST_CASE("A procedural sphere is pickable without triangles", "[gamelib][raycast]")
{
	auto caster = game::Raycaster();
	caster.AddInstance(caster.AddSphere(1.0f), glm::mat4(1.0f));

	const auto hit = caster.Raycast(Forward(0.0f, 0.0f));
	REQUIRE(hit.has_value());
	CHECK(hit->submeshIndex == 0);
	CHECK(std::abs(hit->t - 4.0f) < c_Tolerance);

	CHECK_FALSE(caster.Raycast(Forward(2.0f, 0.0f)).has_value());
}

TEST_CASE("Geometry the raycaster cannot read is refused, not misread", "[gamelib][raycast]")
{
	const TestSubmesh submeshes[] = { TriangleAt({ 0.0f, 0.0f }, 0.0f) };
	assetlib::BMesh   mesh        = MakeMesh(submeshes);

	auto caster = game::Raycaster();

	CHECK_THROWS_AS(caster.AddMesh(mesh, 1), std::runtime_error);
	CHECK_THROWS_AS(caster.AddInstance(99, glm::mat4(1.0f)), std::runtime_error);

	mesh.submeshes[0].layout.attributes[0].offset = 100;  // reads past every vertex's stride
	CHECK_THROWS_AS(caster.AddMesh(mesh, 0), std::runtime_error);

	mesh.submeshes[0].layout.attributes[0].semantic = assetlib::VertexSemantic::kNormal;
	CHECK_THROWS_AS(caster.AddMesh(mesh, 0), std::runtime_error);
}

TEST_CASE("Clear forgets every placed instance", "[gamelib][raycast]")
{
	const TestSubmesh     submeshes[] = { TriangleAt({ 0.0f, 0.0f }, 0.0f) };
	const assetlib::BMesh mesh        = MakeMesh(submeshes);

	auto caster = game::Raycaster();
	caster.AddInstance(caster.AddMesh(mesh, 0), glm::mat4(1.0f));
	REQUIRE(caster.Raycast(Forward(0.0f, 0.0f)).has_value());

	caster.Clear();
	CHECK_FALSE(caster.Raycast(Forward(0.0f, 0.0f)).has_value());
}
