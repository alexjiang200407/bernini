#pragma once
#include <array>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/container_info.h>

#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "MountAt.h"
#include "bmesh_texture.h"
#include <assetlib/project_layout.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>

/**
 * The on-disk scaffolding the reference-graph suites share: a scratch data root, and writers for
 * the smallest real asset of each kind. Real files rather than stubs, because the graph and the
 * operations planned on it (delete, rename) read them back through the same IO the editor uses.
 */
namespace assetlib::test
{
	namespace fs = std::filesystem;

	// A scratch data root that cleans up after itself.
	struct DataRoot
	{
		fs::path path;

		explicit DataRoot(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Authored/Materials");
			fs::create_directories(path / "Derived/Meshes");
			fs::create_directories(path / "Derived/SourceTextures");
		}
		~DataRoot() { fs::remove_all(path); }

		AssetStore
		Source() const
		{
			return AssetStore(path);
		}

		AssetRefGraph
		Scan() const
		{
			return AssetRefGraph::Scan(Source());
		}
	};

	// Writes a 16 x 16 uncompressed RGBA8 .ktx2 whose every texel is `rgba`.
	inline void
	WriteSource(const fs::path& path, std::array<uint8_t, 4> rgba)
	{
		constexpr uint32_t c_Size = 16;

		std::vector<std::byte> pixels(static_cast<size_t>(c_Size) * c_Size * 4);
		for (size_t t = 0; t < static_cast<size_t>(c_Size) * c_Size; ++t)
			for (size_t c = 0; c < 4; ++c) pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);

		fs::create_directories(path.parent_path());
		writeKTX2(rgba8ToImage(pixels, c_Size, c_Size), path, false, Ktx2Compression::kNone);
	}

	/**
	 * Bakes a material whose base colour reads `source` (relative to the root), saves it as
	 * `Authored/Materials/<name>`, and returns it -- so a test can name the maps the bake wrote.
	 */
	inline BMaterial
	BakeAndSave(const DataRoot& root, const char* name, const char* source)
	{
		BMaterial material;
		material.pbr.routes[0] = { source, 0 };

		StoreAt(root.path).BakeMaterial(material);
		StoreAt(root.path).Save(material, std::string("Authored/Materials/") + name);
		return material;
	}

	/** A minimal but loadable mesh whose submeshes name `materials`, one slot each. */
	inline BMesh
	MakeMesh(const std::vector<std::string>& materials, const std::string& skeleton = {})
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

		for (uint32_t i = 0; i < materials.size(); ++i)
		{
			Submesh submesh{};
			submesh.indexType = IndexType::kUint16;
			submesh.material  = i;
			mesh.submeshes.push_back(submesh);
		}

		mesh.meshes    = { Mesh{ 0, static_cast<uint32_t>(materials.size()), 0 } };
		mesh.materials = materials;
		mesh.skeleton  = skeleton;
		return mesh;
	}

	/** Writes the mesh and returns the key it went to, so no caller re-spells the category. */
	inline std::string
	SaveMesh(
		const DataRoot&                 root,
		const char*                     name,
		const std::vector<std::string>& materials,
		const std::string&              skeleton = {})
	{
		const std::string key = KeyIn(c_MeshesDirectoryName, name);
		SaveAt(MakeMesh(materials, skeleton), root.path / key);
		return key;
	}

	/** The referrers of `asset`, as plain paths, so a test can compare against what it wrote. */
	inline std::vector<std::string>
	ReferrerPaths(const AssetRefGraph& graph, std::string_view asset)
	{
		std::vector<std::string> out;
		for (const AssetRef& ref : graph.ReferrersOf(asset)) out.push_back(ref.referrer);
		return out;
	}

	/**
	 * A whole environment on disk: a `.bsky` and a `.benvl` naming their sources and baked maps, and a
	 * `.benv` composing the pair. The files the routes name are written too, so nothing lands in
	 * `broken` and each edge is judged on the reference rather than on the absence.
	 */
	struct Environment
	{
		std::string env      = "Authored/Environments/forest.benv";
		std::string sky      = "Derived/Sky/forest.bsky";
		std::string lighting = "Derived/EnvLighting/forest.benvl";

		std::string skySource  = "Derived/SourceTextures/forest.ktx2";
		std::string skyBaked   = "Derived/BakedTextures/forest_sky.ktx2";
		std::string prefilter  = "Derived/BakedTextures/forest_prefilter.ktx2";
		std::string irradiance = "Derived/BakedTextures/forest_irradiance.ktx2";
	};

	inline Environment
	WriteEnvironment(const DataRoot& root)
	{
		const Environment e;

		for (const std::string* dir : { &e.env, &e.sky, &e.lighting })
			fs::create_directories((root.path / *dir).parent_path());
		fs::create_directories(root.path / "Derived/BakedTextures");

		for (const std::string* map : { &e.skySource, &e.skyBaked, &e.prefilter, &e.irradiance })
			WriteSource(root.path / *map, { { 10, 20, 30, 255 } });

		// Stamped as the source measures, so the routes read as current: a sandbox exercising
		// the reference graph must not trip pack's stale-bake path over placeholder pixels.
		const SourceStamp current = stampOf(root.path / e.skySource);

		BSky sky;
		sky.name       = "forest";
		sky.sky.source = e.skySource;
		sky.sky.baked  = e.skyBaked;
		sky.sky.stamp  = current;
		StoreAt(root.path).Save(sky, e.sky);

		BEnvLighting lighting;
		lighting.name              = "forest";
		lighting.prefilter.source  = e.skySource;
		lighting.prefilter.baked   = e.prefilter;
		lighting.prefilter.stamp   = current;
		lighting.irradiance.source = e.skySource;
		lighting.irradiance.baked  = e.irradiance;
		lighting.irradiance.stamp  = current;
		StoreAt(root.path).Save(lighting, e.lighting);

		BEnv env;
		env.name     = "forest";
		env.sky      = e.sky;
		env.lighting = e.lighting;
		StoreAt(root.path).Save(env, e.env);

		return e;
	}
}
