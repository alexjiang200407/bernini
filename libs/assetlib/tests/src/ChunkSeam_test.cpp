#include <assetlib/banim_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/pak_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <core/file/LooseFileSystem.h>

#include "CountingFileSystem.h"
#include "mounted_io.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	struct Scratch
	{
		fs::path path;

		explicit Scratch(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Meshes");
			fs::create_directories(path / "Skeletons");
			fs::create_directories(path / "Animations");
		}
		~Scratch() { fs::remove_all(path); }
	};

	// Deliberately bulky: the ranged-read assertions below only mean something when the geometry
	// dwarfs the reference chunks, which is the real shape of a mesh in a project.
	BMesh
	MakeMesh()
	{
		BMesh mesh;

		Node root{};
		root.localTransform = { glm::vec3(0.0f),
			                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                    glm::vec3(1.0f) };
		root.parent         = c_InvalidIndex;
		root.firstChild     = c_InvalidIndex;
		root.nextSibling    = c_InvalidIndex;
		root.mesh           = 0;
		root.nameOffset     = 0;
		mesh.nodes          = { root };
		mesh.roots          = { 0 };

		Submesh submesh{};
		submesh.vertexCount  = 3;
		submesh.indexCount   = 3;
		submesh.indexType    = IndexType::kUint16;
		submesh.meshletCount = 1;
		submesh.material     = 0;
		submesh.aabbMin      = glm::vec3(0.0f);
		submesh.aabbMax      = glm::vec3(1.0f);
		mesh.submeshes       = { submesh };

		mesh.meshes = { Mesh{ 0, 1, 0 } };

		Meshlet meshlet{};
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 1;
		meshlet.boundingCenter = glm::vec3(0.5f);
		meshlet.boundingRadius = 1.0f;
		mesh.meshlets          = { meshlet };
		mesh.meshletVertices   = { 0, 1, 2 };
		mesh.meshletTriangles  = { 0, 1, 2 };

		mesh.vertexData.resize(256u * 1024u, std::byte{ 0x7 });
		mesh.indexData.resize(64u * 1024u, std::byte{ 0x3 });

		mesh.materials = { "Materials/a.bmaterial", "Materials/b.bmaterial" };
		mesh.skeleton  = "Skeletons/rig.bskel";
		return mesh;
	}

	Skeleton
	MakeSkeleton()
	{
		Skeleton skeleton;

		Bone root{};
		root.parent      = c_InvalidIndex;
		root.inverseBind = glm::mat4(1.0f);
		root.nameOffset  = skeleton.stringPool.add("root");
		skeleton.bones   = { root };
		return skeleton;
	}

	AnimationSet
	MakeAnimations()
	{
		AnimationSet animations;
		animations.skeleton  = "Skeletons/rig.bskel";
		animations.boneCount = 1;

		AnimationClip clip{};
		clip.frameCount    = 2;
		clip.sampleRate    = 30.0f;
		clip.firstSample   = 0;
		clip.nameOffset    = animations.stringPool.add("idle");
		animations.clips   = { clip };
		animations.samples = { Transform{}, Transform{} };
		return animations;
	}

	// The same three containers, loose in `root` and packed into `root/Data.bpak`.
	void
	Stage(const fs::path& root)
	{
		save(MakeMesh(), root / "Meshes/kirk.bmesh");
		saveSkeleton(MakeSkeleton(), root / "Skeletons/rig.bskel");
		saveAnimations(MakeAnimations(), root / "Animations/idle.banim");

		const core::file::LooseFileSystem loose(root);

		PakWriter writer(root / "Data.bpak");
		for (const std::string& entry : loose.Enumerate())
			writer.Add(entry, loose.Read(entry), loose.Stat(entry).value());
		writer.Finish();
	}
}

TEST_CASE("a chunked container loads the same from a directory and from an archive", "[chunkseam]")
{
	const Scratch scratch("seam_equal");
	Stage(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	SECTION(".bmesh")
	{
		const BMesh direct = load(scratch.path / "Meshes/kirk.bmesh");

		for (const core::file::IFileSystem* mount :
		     { static_cast<const core::file::IFileSystem*>(&loose),
		       static_cast<const core::file::IFileSystem*>(&pak) })
		{
			const BMesh mounted = load(*mount, "Meshes/kirk.bmesh");

			CHECK(mounted.vertexData == direct.vertexData);
			CHECK(mounted.indexData == direct.indexData);
			CHECK(mounted.meshletVertices == direct.meshletVertices);
			CHECK(mounted.meshletTriangles == direct.meshletTriangles);
			CHECK(mounted.materials == direct.materials);
			CHECK(mounted.skeleton == direct.skeleton);
			CHECK(mounted.nodes.size() == direct.nodes.size());
			CHECK(mounted.submeshes.size() == direct.submeshes.size());

			// The strongest form of "identical": re-serializing both gives the same bytes.
			CHECK(serialize(mounted) == serialize(direct));
		}
	}

	SECTION(".bmesh references, without its geometry")
	{
		const MeshRefs direct = loadMeshRefs(scratch.path / "Meshes/kirk.bmesh");

		CHECK(loadMeshRefs(loose, "Meshes/kirk.bmesh").materials == direct.materials);
		CHECK(loadMeshRefs(pak, "Meshes/kirk.bmesh").materials == direct.materials);
		CHECK(loadMeshRefs(pak, "Meshes/kirk.bmesh").skeleton == direct.skeleton);
	}

	SECTION(".bskel")
	{
		const Skeleton direct = loadSkeleton(scratch.path / "Skeletons/rig.bskel");

		CHECK(
			serializeSkeleton(loadSkeleton(loose, "Skeletons/rig.bskel")) ==
			serializeSkeleton(direct));
		CHECK(
			serializeSkeleton(loadSkeleton(pak, "Skeletons/rig.bskel")) ==
			serializeSkeleton(direct));
	}

	SECTION(".banim")
	{
		const AnimationSet direct = loadAnimations(scratch.path / "Animations/idle.banim");

		CHECK(
			serializeAnimations(loadAnimations(loose, "Animations/idle.banim")) ==
			serializeAnimations(direct));
		CHECK(
			serializeAnimations(loadAnimations(pak, "Animations/idle.banim")) ==
			serializeAnimations(direct));

		CHECK(loadAnimationSkeletonPath(loose, "Animations/idle.banim") == direct.skeleton);
		CHECK(loadAnimationSkeletonPath(pak, "Animations/idle.banim") == direct.skeleton);
	}
}

TEST_CASE("a reference read stays a ranged read through the seam", "[chunkseam]")
{
	const Scratch scratch("seam_ranged");
	Stage(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	const uint64_t meshSize = loose.Stat("Meshes/kirk.bmesh").value().size;

	// The container has to be big enough for the distinction to exist at all.
	REQUIRE(meshSize > 256u * 1024u);

	SECTION("loadMeshRefs reads a few hundred bytes of a 300 KB mesh")
	{
		for (const core::file::IFileSystem* mount :
		     { static_cast<const core::file::IFileSystem*>(&loose),
		       static_cast<const core::file::IFileSystem*>(&pak) })
		{
			CountingFileSystem counting(*mount);
			(void)loadMeshRefs(counting, "Meshes/kirk.bmesh");

			// Header + chunk table + the two reference chunks. The bound is generous on purpose:
			// what it rules out is a whole-file read, which is the regression that matters.
			CHECK(counting.bytesRead < 4096u);
			CHECK(counting.bytesRead < meshSize / 16u);
		}
	}

	SECTION("loadAnimationSkeletonPath is ranged too")
	{
		CountingFileSystem counting(pak);
		(void)loadAnimationSkeletonPath(counting, "Animations/idle.banim");

		CHECK(counting.bytesRead < 4096u);
	}

	SECTION("a full load does read the whole container, which is the contrast")
	{
		CountingFileSystem counting(pak);
		(void)load(counting, "Meshes/kirk.bmesh");

		CHECK(counting.bytesRead == meshSize);
		CHECK(counting.reads == 1u);
	}
}
