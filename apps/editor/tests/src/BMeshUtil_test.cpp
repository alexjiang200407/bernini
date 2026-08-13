#include "Mesh/BMeshUtil.h"

#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMesh.h>

namespace
{
	assetlib::Submesh
	MakeSubmesh(bool skinned)
	{
		assetlib::Submesh submesh{};
		submesh.indexType             = assetlib::IndexType::kUint16;
		submesh.layout.attributeCount = 1;
		submesh.layout.attributes[0].semantic =
			skinned ? assetlib::VertexSemantic::kJoints0 : assetlib::VertexSemantic::kPosition;
		submesh.layout.attributes[0].format =
			skinned ? assetlib::VertexFormat::kUint16x4 : assetlib::VertexFormat::kFloat32x3;
		return submesh;
	}

	/**
	 * An armature carrying a transform, with a skinned mesh and a static attachment hanging off it --
	 * the shape a rigged glTF exports as, and the one where the two placement rules disagree.
	 *
	 *   node 0  armature   (yaw 90 degrees, scale 0.01)
	 *     node 1  mesh 0   skinned
	 *     node 2  mesh 1   static attachment
	 */
	assetlib::BMesh
	MakeRig()
	{
		assetlib::BMesh mesh;
		mesh.nodes.resize(3);
		for (assetlib::Node& node : mesh.nodes)
		{
			node.localTransform = { glm::vec3(0.0f),
				                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                    glm::vec3(1.0f) };
			node.parent         = assetlib::c_InvalidIndex;
			node.firstChild     = assetlib::c_InvalidIndex;
			node.nextSibling    = assetlib::c_InvalidIndex;
			node.mesh           = assetlib::c_InvalidIndex;
			node.nameOffset     = 0;
		}

		mesh.nodes[0].localTransform = {
			glm::vec3(0.0f, 0.5f, 0.0f),
			glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			glm::vec3(0.01f)
		};
		mesh.nodes[0].firstChild  = 1;
		mesh.nodes[1].parent      = 0;
		mesh.nodes[1].mesh        = 0;
		mesh.nodes[1].nextSibling = 2;
		mesh.nodes[2].parent      = 0;
		mesh.nodes[2].mesh        = 1;
		mesh.roots.push_back(0);

		mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		mesh.meshes.push_back({ .firstSubmesh = 1, .submeshCount = 1, .nameOffset = 0 });
		mesh.submeshes.push_back(MakeSubmesh(true));
		mesh.submeshes.push_back(MakeSubmesh(false));
		mesh.skeleton = "Skeletons/rig.bskel";

		return mesh;
	}
}

TEST_CASE("A skinned mesh is placed in the skin's space, not the armature's", "[bmesh]")
{
	const assetlib::BMesh mesh = MakeRig();

	// The armature's transform is genuinely there -- this is not a rig that would look the same
	// either way.
	REQUIRE(bmesh::GetWorldTransform(mesh, 1) != glm::mat4(1.0f));

	REQUIRE(bmesh::GetInstanceTransform(mesh, 1) == glm::mat4(1.0f));
}

TEST_CASE("A static attachment still hangs off the bone it is parented to", "[bmesh]")
{
	const assetlib::BMesh mesh = MakeRig();

	REQUIRE(bmesh::GetInstanceTransform(mesh, 2) == bmesh::GetWorldTransform(mesh, 2));
	REQUIRE(bmesh::GetInstanceTransform(mesh, 2) != glm::mat4(1.0f));
}

TEST_CASE("Only the skinned mesh's own node is exempt", "[bmesh]")
{
	const assetlib::BMesh mesh = MakeRig();

	REQUIRE(assetlib::isSkinned(mesh));
	REQUIRE(assetlib::isSkinned(mesh, 0));
	REQUIRE_FALSE(assetlib::isSkinned(mesh, 1));

	// The armature itself references no mesh, so it is not exempt from anything -- and asking about
	// a mesh index that is not there answers "not skinned" rather than throwing.
	REQUIRE_FALSE(assetlib::isSkinned(mesh, assetlib::c_InvalidIndex));
	REQUIRE_FALSE(assetlib::isSkinned(mesh, 2));
	REQUIRE(bmesh::GetInstanceTransform(mesh, 0) == bmesh::GetWorldTransform(mesh, 0));
}
