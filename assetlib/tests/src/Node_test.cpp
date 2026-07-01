#include <assetlib/bmesh/Bmesh.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib::bmesh;

TEST_CASE("first-child / next-sibling links reconstruct a node's children", "[bmesh][node]")
{
	// root(0) -> children a(1), b(2), c(3)
	BMesh mesh;
	mesh.nodes.resize(4);
	for (auto& node : mesh.nodes)
	{
		node.parent      = c_InvalidIndex;
		node.firstChild  = c_InvalidIndex;
		node.nextSibling = c_InvalidIndex;
		node.mesh        = c_InvalidIndex;
	}
	mesh.nodes[0].firstChild  = 1;
	mesh.nodes[1].parent      = 0;
	mesh.nodes[1].nextSibling = 2;
	mesh.nodes[2].parent      = 0;
	mesh.nodes[2].nextSibling = 3;
	mesh.nodes[3].parent      = 0;

	std::vector<uint32_t> children;
	for (auto c = mesh.nodes[0].firstChild; c != c_InvalidIndex; c = mesh.nodes[c].nextSibling)
		children.push_back(c);

	REQUIRE(children == std::vector<uint32_t>{ 1, 2, 3 });
}

TEST_CASE("toMatrix composes translation, rotation and scale", "[bmesh][node]")
{
	Transform transform{ glm::vec3(1.0f, 2.0f, 3.0f),
		                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		                 glm::vec3(2.0f) };

	const auto matrix = toMatrix(transform);

	// Translation lands in the 4th column.
	REQUIRE(matrix[3][0] == Catch::Approx(1.0f));
	REQUIRE(matrix[3][1] == Catch::Approx(2.0f));
	REQUIRE(matrix[3][2] == Catch::Approx(3.0f));
	// Identity rotation with uniform scale of 2 on the diagonal.
	REQUIRE(matrix[0][0] == Catch::Approx(2.0f));
	REQUIRE(matrix[1][1] == Catch::Approx(2.0f));
	REQUIRE(matrix[2][2] == Catch::Approx(2.0f));
}
