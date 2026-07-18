#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/image_io.h>
#include <bgl/IGraphics.h>
#include <bgl/MaterialType.h>
#include <gamelib/AssetManager.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	AssetSceneDesc()
	{
		// Roomy enough for the procedural case: a cube plus an 8x8 sphere is a few thousand indices.
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 8;
		desc.maxSubmeshes            = 32;
		desc.maxMeshlets             = 256;
		desc.maxVertexBufferByteSize = 65536;
		desc.maxIndices              = 8192;
		desc.maxPbrMaterials         = 8;
		desc.maxLoosePbrMaterials    = 8;
		return desc;
	}

	// A scratch data root that cleans up after itself.
	struct DataRoot
	{
		std::filesystem::path path;

		explicit DataRoot(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}
		~DataRoot() { std::filesystem::remove_all(path); }
	};

	// A 1x1 uncompressed .ktx2, so the manager has something real to upload.
	void
	WriteTexture(const std::filesystem::path& path)
	{
		auto image      = assetlib::ImageData();
		image.width     = 1;
		image.height    = 1;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;

		image.pixels = core::fixed_buffer<std::byte>(4);
		std::fill_n(image.pixels.data(), 4, std::byte{ 0xFF });
		image.subresources.push_back({ 0, 4, 4 });

		std::filesystem::create_directories(path.parent_path());
		assetlib::writeKTX2(image, path, false, assetlib::Ktx2Compression::kNone);
	}

	void
	WriteBakedMaterial(
		const std::filesystem::path& path,
		const std::string&           baseColor,
		assetlib::AlphaMode          alphaMode = assetlib::AlphaMode::kOpaque)
	{
		auto material                 = assetlib::BMaterial();
		material.mode                 = assetlib::MaterialMode::kBaked;
		material.pbr.baseColorTexture = baseColor;
		material.pbr.alphaMode        = alphaMode;

		std::filesystem::create_directories(path.parent_path());
		assetlib::saveMaterial(material, path);
	}

	// A minimal .bmesh: one mesh, one submesh per entry in `materialIndices`, each a single meshlet
	// of one triangle. `materials` are the data-root-relative .bmaterial paths it names.
	void
	WriteMesh(
		const std::filesystem::path& path,
		std::span<const std::string> materials,
		std::span<const uint32_t>    materialIndices)
	{
		constexpr uint16_t kStride = 12;  // one float32x3 position

		auto mesh = assetlib::BMesh();
		mesh.stringPool.push_back('\0');
		mesh.materials.assign(materials.begin(), materials.end());

		mesh.vertexData.resize(materialIndices.size() * 3 * kStride);

		uint32_t vertexCursor = 0;
		for (const uint32_t materialIndex : materialIndices)
		{
			auto meshlet           = assetlib::Meshlet();
			meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
			meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
			meshlet.vertexCount    = 3;
			meshlet.triangleCount  = 1;
			meshlet.boundingCenter = glm::vec3(0.0f);
			meshlet.boundingRadius = 1.0f;

			const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
			mesh.meshlets.push_back(meshlet);

			for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
			for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = kStride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = vertexCursor * kStride;
			submesh.vertexCount           = 3;
			submesh.firstMeshlet          = firstMeshlet;
			submesh.meshletCount          = 1;
			submesh.material              = materialIndex;
			submesh.aabbMin               = glm::vec3(-1.0f);
			submesh.aabbMax               = glm::vec3(1.0f);
			submesh.nameOffset            = 0;
			mesh.submeshes.push_back(submesh);

			vertexCursor += 3;
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = static_cast<uint32_t>(materialIndices.size());
		entry.nameOffset   = 0;
		mesh.meshes.push_back(entry);

		auto node   = assetlib::Node();
		node.mesh   = 0;
		node.parent = assetlib::c_InvalidIndex;
		mesh.nodes.push_back(node);
		mesh.roots.push_back(0);

		std::filesystem::create_directories(path.parent_path());
		assetlib::save(mesh, path);
	}

	// A scene + view + manager over a scratch data root, which is what every case here needs.
	struct Fixture
	{
		DataRoot                          root;
		bgl::GraphicsRef                  gfx;
		bgl::SceneRef                     scene;
		bgl::SceneViewRef                 view;
		std::optional<game::AssetManager> assets;

		explicit Fixture(const char* name) : root(name), gfx(bgl::CreateGraphics(HeadlessOptions()))
		{
			scene = gfx->CreateScene(AssetSceneDesc());
			view  = gfx->CreateSceneView(scene, 16);
			assets.emplace(scene, root.path);
		}

		// The manager is non-copyable, so these would be implicitly deleted anyway; say so, because
		// the tests build with /Wall /WX.
		Fixture(const Fixture&) = delete;
		Fixture(Fixture&&)      = delete;

		Fixture&
		operator=(const Fixture&) = delete;

		Fixture&
		operator=(Fixture&&) = delete;

		game::AssetManager&
		operator*()
		{
			return *assets;
		}
	};
}

TEST_CASE("AssetManager carries a material's alpha mode into its layer type", "[gamelib][assets]")
{
	// The seam that lets an imported glTF's BLEND material render translucent: the baked alpha mode
	// has to reach bgl as the matching LayerType, which is what decides the PSO the submesh draws.
	Fixture fx("bernini_am_alphamode");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "opaque.bmaterial", "Textures/a.ktx2");
	WriteBakedMaterial(
		fx.root.path / "Materials" / "cutout.bmaterial",
		"Textures/a.ktx2",
		assetlib::AlphaMode::kMask);
	WriteBakedMaterial(
		fx.root.path / "Materials" / "blend.bmaterial",
		"Textures/a.ktx2",
		assetlib::AlphaMode::kBlend);

	CHECK((*fx).AcquireMaterial("Materials/opaque.bmaterial").layerType == bgl::LayerType::kOpaque);
	CHECK((*fx).AcquireMaterial("Materials/cutout.bmaterial").layerType == bgl::LayerType::kMask);
	CHECK((*fx).AcquireMaterial("Materials/blend.bmaterial").layerType == bgl::LayerType::kBlend);
}

TEST_CASE("AssetManager shares an asset by path and counts its references", "[gamelib][assets]")
{
	// Identity is the reason it exists: a material's nine routes commonly name the same file, baked
	// maps are shared between materials, and submeshes share `.bmaterial`s.
	Fixture fx("bernini_am_identity");
	WriteTexture(fx.root.path / "Textures" / "shared.ktx2");

	SECTION("a texture asked for twice is uploaded once, and counted twice")
	{
		const auto first  = (*fx).AcquireTexture("Textures/shared.ktx2");
		const auto second = (*fx).AcquireTexture("Textures/shared.ktx2");

		REQUIRE(first.textureSlot);
		CHECK(first.textureSlot == second.textureSlot);
		CHECK((*fx).TextureRefCount(first) == 2);

		// One release is not enough: the second reference still holds it.
		(*fx).ReleaseTexture(first);
		CHECK((*fx).TextureRefCount(first) == 1);

		(*fx).ReleaseTexture(first);
		CHECK((*fx).TextureRefCount(first) == 0);  // gone
	}

	SECTION("an empty path is absent, not an error")
	{
		// The scene substitutes white / a flat normal for an unset map.
		CHECK_FALSE((*fx).AcquireTexture("").textureSlot);
	}

	SECTION("a missing texture is an error, not a silent default")
	{
		CHECK_THROWS_AS((*fx).AcquireTexture("Textures/absent.ktx2"), std::runtime_error);
	}
}

TEST_CASE("AssetManager acquires a material's textures with it", "[gamelib][assets]")
{
	Fixture fx("bernini_am_material");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m1.bmaterial", "Textures/a.ktx2");

	SECTION("loading a material uploads the textures it names")
	{
		const bgl::MaterialHandle mat = (*fx).AcquireMaterial("Materials/m0.bmaterial");

		REQUIRE(mat.IsValid());
		CHECK(mat.materialType == bgl::MaterialType::kPBR);
		CHECK((*fx).MaterialRefCount(mat) == 1);

		const auto tex = (*fx).AcquireTexture("Textures/a.ktx2");  // +1 on top of the material's
		CHECK((*fx).TextureRefCount(tex) == 2);
		(*fx).ReleaseTexture(tex);
	}

	SECTION("releasing a material releases the textures it held")
	{
		const bgl::MaterialHandle mat = (*fx).AcquireMaterial("Materials/m0.bmaterial");

		const auto tex = (*fx).AcquireTexture("Textures/a.ktx2");
		(*fx).ReleaseTexture(tex);
		CHECK((*fx).TextureRefCount(tex) == 1);  // held only by the material now

		(*fx).ReleaseMaterial(mat);
		CHECK((*fx).MaterialRefCount(mat) == 0);
		CHECK((*fx).TextureRefCount(tex) == 0);  // cascaded
	}

	SECTION("a texture shared by two materials survives one of them going")
	{
		// This is the case a naive teardown gets wrong: deleting m0 must not pull the texture out from
		// under m1, which is still sampling it.
		const bgl::MaterialHandle m0 = (*fx).AcquireMaterial("Materials/m0.bmaterial");
		const bgl::MaterialHandle m1 = (*fx).AcquireMaterial("Materials/m1.bmaterial");

		const auto tex = (*fx).AcquireTexture("Textures/a.ktx2");
		(*fx).ReleaseTexture(tex);
		CHECK((*fx).TextureRefCount(tex) == 2);  // one from each material

		(*fx).ReleaseMaterial(m0);
		CHECK((*fx).MaterialRefCount(m0) == 0);
		CHECK((*fx).TextureRefCount(tex) == 1);  // m1 still holds it

		(*fx).ReleaseMaterial(m1);
		CHECK((*fx).TextureRefCount(tex) == 0);
	}
}

TEST_CASE("AssetManager acquires a mesh's whole tree", "[gamelib][assets]")
{
	Fixture fx("bernini_am_mesh");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");

	const auto materials       = std::vector<std::string>{ "Materials/m0.bmaterial" };
	const auto materialIndices = std::vector<uint32_t>{ 0 };
	WriteMesh(fx.root.path / "Meshes" / "one.bmesh", materials, materialIndices);

	SECTION("mesh -> materials -> textures, acquired and released transitively")
	{
		const bgl::GeomHandle geom = (*fx).AcquireMesh("Meshes/one.bmesh");
		REQUIRE(geom.IsValid());
		CHECK((*fx).GeomRefCount(geom) == 1);

		const bgl::MaterialHandle mat = (*fx).AcquireMaterial("Materials/m0.bmaterial");
		const auto                tex = (*fx).AcquireTexture("Textures/a.ktx2");

		// The mesh's submesh holds a reference to the material, which holds one to the texture -- so
		// each is one above the reference we just took ourselves.
		CHECK((*fx).MaterialRefCount(mat) == 2);
		CHECK((*fx).TextureRefCount(tex) == 2);

		(*fx).ReleaseMaterial(mat);
		(*fx).ReleaseTexture(tex);

		(*fx).ReleaseGeom(geom);
		CHECK((*fx).GeomRefCount(geom) == 0);
		CHECK((*fx).MaterialRefCount(mat) == 0);  // cascaded
		CHECK((*fx).TextureRefCount(tex) == 0);   // cascaded
		CHECK_FALSE(fx.scene->IsGeomAlive(geom));
	}

	SECTION("the same mesh asked for twice is uploaded once")
	{
		const bgl::GeomHandle first  = (*fx).AcquireMesh("Meshes/one.bmesh");
		const bgl::GeomHandle second = (*fx).AcquireMesh("Meshes/one.bmesh");

		CHECK(first.handle.index == second.handle.index);
		CHECK((*fx).GeomRefCount(first) == 2);

		(*fx).ReleaseGeom(first);
		CHECK(fx.scene->IsGeomAlive(first));  // the second reference still holds it
		(*fx).ReleaseGeom(second);
		CHECK_FALSE(fx.scene->IsGeomAlive(first));
	}
}

TEST_CASE("AssetManager: an instance keeps its geometry alive", "[gamelib][assets]")
{
	// The whole reason the manager owns instances. bgl's DeleteGeom no longer refuses a geom with live
	// instances -- it cannot see them -- so an instance holding a reference is what makes "the last
	// reference is gone" mean "nothing is drawing it".
	Fixture fx("bernini_am_instance");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");

	const auto materials       = std::vector<std::string>{ "Materials/m0.bmaterial" };
	const auto materialIndices = std::vector<uint32_t>{ 0 };
	WriteMesh(fx.root.path / "Meshes" / "one.bmesh", materials, materialIndices);

	const bgl::GeomHandle geom = (*fx).AcquireMesh("Meshes/one.bmesh");

	const bgl::MeshInstanceHandle inst = (*fx).CreateInstance(fx.view, geom, glm::mat4(1.0f));
	REQUIRE(inst.IsValid());
	CHECK((*fx).GeomRefCount(geom) == 2);  // ours + the instance's

	// Releasing our reference while the instance lives must NOT delete the geometry.
	(*fx).ReleaseGeom(geom);
	CHECK((*fx).GeomRefCount(geom) == 1);
	CHECK(fx.scene->IsGeomAlive(geom));

	// Destroying the instance drops the last reference, and only now is it deleted.
	(*fx).DestroyInstance(fx.view, inst);
	CHECK((*fx).GeomRefCount(geom) == 0);
	CHECK_FALSE(fx.scene->IsGeomAlive(geom));
}

TEST_CASE("AssetManager places instances into more than one view", "[gamelib][assets]")
{
	// One scene, more than one view: the Level Editor's viewport and the Material Editor's preview both
	// draw the editor's single scene. The manager is the scene's, so geometry it holds is one upload
	// counted once however many views instance it, and each instance names the view it lives in.
	Fixture fx("bernini_am_multiview");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");

	const auto materials       = std::vector<std::string>{ "Materials/m0.bmaterial" };
	const auto materialIndices = std::vector<uint32_t>{ 0 };
	WriteMesh(fx.root.path / "Meshes" / "one.bmesh", materials, materialIndices);

	const bgl::SceneViewRef second = fx.gfx->CreateSceneView(fx.scene, 16);

	const bgl::GeomHandle geom = (*fx).AcquireMesh("Meshes/one.bmesh");

	// A null view is refused here, not left to crash inside bgl.
	REQUIRE_THROWS_AS(
		(*fx).CreateInstance(bgl::SceneViewRef{}, geom, glm::mat4(1.0f)),
		bgl::SceneError);

	// The same geometry, instanced once in each view.
	const bgl::MeshInstanceHandle inFirst  = (*fx).CreateInstance(fx.view, geom, glm::mat4(1.0f));
	const bgl::MeshInstanceHandle inSecond = (*fx).CreateInstance(second, geom, glm::mat4(1.0f));

	// Ours, plus one per instance -- across both views.
	CHECK((*fx).GeomRefCount(geom) == 3);

	(*fx).ReleaseGeom(geom);
	CHECK((*fx).GeomRefCount(geom) == 2);  // the two instances still hold it, one per view
	CHECK(fx.scene->IsGeomAlive(geom));

	// Destroying the first view's instance does not disturb the second's.
	(*fx).DestroyInstance(fx.view, inFirst);
	CHECK((*fx).GeomRefCount(geom) == 1);
	CHECK(fx.scene->IsGeomAlive(geom));

	(*fx).DestroyInstance(second, inSecond);
	CHECK((*fx).GeomRefCount(geom) == 0);
	CHECK_FALSE(fx.scene->IsGeomAlive(geom));
}

TEST_CASE("AssetManager swaps a material's texture in place", "[gamelib][assets]")
{
	Fixture fx("bernini_am_swap_texture");
	WriteTexture(fx.root.path / "Textures" / "old.ktx2");
	WriteTexture(fx.root.path / "Textures" / "new.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/old.ktx2");

	const bgl::MaterialHandle mat = (*fx).AcquireMaterial("Materials/m0.bmaterial");

	const auto oldTex = (*fx).AcquireTexture("Textures/old.ktx2");
	(*fx).ReleaseTexture(oldTex);
	REQUIRE((*fx).TextureRefCount(oldTex) == 1);  // held by the material

	(*fx).SetMaterialTexture(mat, game::AssetManager::TextureSlot::kBaseColor, "Textures/new.ktx2");

	const auto newTex = (*fx).AcquireTexture("Textures/new.ktx2");
	(*fx).ReleaseTexture(newTex);

	// The old texture was released, the new one acquired, and -- the point of updating in place --
	// the material's handle is unchanged, so every submesh bound to it followed without rebinding.
	CHECK((*fx).TextureRefCount(oldTex) == 0);
	CHECK((*fx).TextureRefCount(newTex) == 1);
	CHECK((*fx).MaterialRefCount(mat) == 1);
	CHECK(mat.handle.index == (*fx).AcquireMaterial("Materials/m0.bmaterial").handle.index);
}

TEST_CASE("AssetManager swaps a submesh's material", "[gamelib][assets]")
{
	Fixture fx("bernini_am_swap_material");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteTexture(fx.root.path / "Textures" / "b.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m1.bmaterial", "Textures/b.ktx2");

	const auto materials       = std::vector<std::string>{ "Materials/m0.bmaterial" };
	const auto materialIndices = std::vector<uint32_t>{ 0 };
	WriteMesh(fx.root.path / "Meshes" / "one.bmesh", materials, materialIndices);

	const bgl::GeomHandle     geom = (*fx).AcquireMesh("Meshes/one.bmesh");
	const bgl::MaterialHandle m0   = (*fx).AcquireMaterial("Materials/m0.bmaterial");
	(*fx).ReleaseMaterial(m0);
	REQUIRE((*fx).MaterialRefCount(m0) == 1);  // held by the geom's one submesh

	(*fx).SetSubmeshMaterial(geom, 0, "Materials/m1.bmaterial");

	const bgl::MaterialHandle m1 = (*fx).AcquireMaterial("Materials/m1.bmaterial");
	(*fx).ReleaseMaterial(m1);

	// The old material was released (and, at zero, took its texture with it); the new one is held by
	// the submesh.
	CHECK((*fx).MaterialRefCount(m0) == 0);
	CHECK((*fx).MaterialRefCount(m1) == 1);

	// Releasing the geom now releases the *new* material, not the one it was loaded with.
	(*fx).ReleaseGeom(geom);
	CHECK((*fx).MaterialRefCount(m1) == 0);
}

TEST_CASE("AssetManager overrides one instance's material", "[gamelib][assets]")
{
	Fixture fx("bernini_am_override_material");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteTexture(fx.root.path / "Textures" / "b.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "skin.bmaterial", "Textures/b.ktx2");

	const auto materials       = std::vector<std::string>{ "Materials/m0.bmaterial" };
	const auto materialIndices = std::vector<uint32_t>{ 0 };
	WriteMesh(fx.root.path / "Meshes" / "one.bmesh", materials, materialIndices);

	const bgl::GeomHandle geom = (*fx).AcquireMesh("Meshes/one.bmesh");

	// Two units, same mesh. One will wear a skin.
	const bgl::MeshInstanceHandle worn  = (*fx).CreateInstance(fx.view, geom, glm::mat4(1.0f));
	const bgl::MeshInstanceHandle plain = (*fx).CreateInstance(fx.view, geom, glm::mat4(1.0f));

	const bgl::MaterialHandle m0 = (*fx).AcquireMaterial("Materials/m0.bmaterial");
	(*fx).ReleaseMaterial(m0);
	REQUIRE((*fx).MaterialRefCount(m0) == 1);  // held by the geom's submesh, once

	(*fx).SetInstanceSubmeshMaterial(fx.view, worn, 0, "Materials/skin.bmaterial");

	const bgl::MaterialHandle skin = (*fx).AcquireMaterial("Materials/skin.bmaterial");
	(*fx).ReleaseMaterial(skin);

	SECTION("the override holds a reference, and the default is untouched")
	{
		// The override is a reference of its own: the skin cannot be destroyed while an instance
		// wears it. The geom's default is not disturbed -- `plain` still draws it.
		CHECK((*fx).MaterialRefCount(skin) == 1);
		CHECK((*fx).MaterialRefCount(m0) == 1);
	}

	SECTION("clearing it releases the material")
	{
		(*fx).ClearInstanceSubmeshMaterial(fx.view, worn, 0);

		CHECK((*fx).MaterialRefCount(skin) == 0);
		CHECK((*fx).MaterialRefCount(m0) == 1);
	}

	SECTION("destroying the instance releases what it wore")
	{
		(*fx).DestroyInstance(fx.view, worn);

		CHECK((*fx).MaterialRefCount(skin) == 0);

		// ...and its sibling, which never overrode anything, is unaffected.
		CHECK((*fx).MaterialRefCount(m0) == 1);
	}

	SECTION("overriding twice releases the first, and re-overriding with the same is not a leak")
	{
		(*fx).SetInstanceSubmeshMaterial(fx.view, worn, 0, "Materials/m0.bmaterial");
		CHECK((*fx).MaterialRefCount(skin) == 0);
		CHECK((*fx).MaterialRefCount(m0) == 2);  // the geom's submesh, and this instance's override

		// Acquire-before-release: re-overriding with the material already worn must not delete it.
		(*fx).SetInstanceSubmeshMaterial(fx.view, worn, 0, "Materials/m0.bmaterial");
		CHECK((*fx).MaterialRefCount(m0) == 2);
	}

	SECTION("a foreign instance or an out-of-range submesh throws")
	{
		REQUIRE_THROWS_AS(
			(*fx).SetInstanceSubmeshMaterial(fx.view, worn, 1, "Materials/skin.bmaterial"),
			bgl::SceneError);
		REQUIRE_THROWS_AS(
			(*fx).SetInstanceSubmeshMaterial(
				fx.view,
				bgl::MeshInstanceHandle{},
				0,
				"Materials/skin.bmaterial"),
			bgl::SceneError);
	}

	(*fx).DestroyInstance(fx.view, plain);
}

TEST_CASE("AssetManager refcounts procedural geometry", "[gamelib][assets]")
{
	// A cube and a sphere have no file to be keyed by, but they are geometry: they are refcounted,
	// deleted at zero, and drop their material's reference on the way out.
	Fixture fx("bernini_am_procedural");
	WriteTexture(fx.root.path / "Textures" / "a.ktx2");
	WriteBakedMaterial(fx.root.path / "Materials" / "m0.bmaterial", "Textures/a.ktx2");

	const bgl::MaterialHandle mat = (*fx).AcquireMaterial("Materials/m0.bmaterial");

	const bgl::GeomHandle cube   = (*fx).CreateCube(mat);
	const bgl::GeomHandle sphere = (*fx).CreateSphere(8, 8, 1.0f, mat);

	REQUIRE(cube.IsValid());
	REQUIRE(sphere.IsValid());

	// Not shared: two procedural geoms are two geoms, even from one material.
	CHECK(cube.handle.index != sphere.handle.index);
	CHECK((*fx).GeomRefCount(cube) == 1);
	CHECK((*fx).GeomRefCount(sphere) == 1);

	// Ours, plus one from each geom's single submesh.
	CHECK((*fx).MaterialRefCount(mat) == 3);

	const bgl::MeshInstanceHandle inst = (*fx).CreateInstance(fx.view, cube, glm::mat4(1.0f));
	CHECK((*fx).GeomRefCount(cube) == 2);
	(*fx).DestroyInstance(fx.view, inst);

	(*fx).ReleaseGeom(cube);
	CHECK_FALSE(fx.scene->IsGeomAlive(cube));
	CHECK((*fx).MaterialRefCount(mat) == 2);

	(*fx).ReleaseGeom(sphere);
	CHECK_FALSE(fx.scene->IsGeomAlive(sphere));
	CHECK((*fx).MaterialRefCount(mat) == 1);

	(*fx).ReleaseMaterial(mat);
	CHECK((*fx).MaterialRefCount(mat) == 0);
}

TEST_CASE("AssetManager keeps its scene alive", "[gamelib][assets]")
{
	// The destructor hands every asset back to the scene, so the manager must *hold* the scene, not
	// borrow it. With a bare reference that was safe only when the caller happened to declare the scene
	// before the manager and destroy it after -- an ordering landmine. A SharedRef makes it moot.
	const DataRoot root("bernini_am_lifetime");
	WriteTexture(root.path / "Textures" / "a.ktx2");

	auto gfx    = bgl::CreateGraphics(HeadlessOptions());
	auto assets = std::optional<game::AssetManager>();

	{
		auto scene = gfx->CreateScene(AssetSceneDesc());
		assets.emplace(scene, root.path);
	}
	// Every handle the caller held is gone. The scene survives on the manager's reference alone.

	const auto tex = (*assets).AcquireTexture("Textures/a.ktx2");
	CHECK(tex.textureSlot);
	CHECK((*assets).TextureRefCount(tex) == 1);

	// And the destructor can still give it back -- the part that used to be a use-after-free.
	CHECK_NOTHROW(assets.reset());
}

TEST_CASE("AssetManager resolves paths against its data root", "[gamelib][assets]")
{
	Fixture fx("bernini_am_root");
	WriteTexture(fx.root.path / "nested" / "deep" / "tex.ktx2");

	CHECK((*fx).DataRoot() == fx.root.path);
	CHECK((*fx).AcquireTexture("nested/deep/tex.ktx2").textureSlot);
}

TEST_CASE("materialTextures names a material's textures in slot order", "[gamelib][assets]")
{
	// The order the record's texture handles parallel. A caller decoding them ahead of time reads it
	// to know what to decode, so it is public and it is pinned here.
	auto baked                 = assetlib::BMaterial();
	baked.mode                 = assetlib::MaterialMode::kBaked;
	baked.pbr.baseColorTexture = "Textures/base.ktx2";
	baked.pbr.normalTexture    = "Textures/nrm.ktx2";
	baked.pbr.ormTexture       = "Textures/orm.ktx2";

	CHECK(
		game::materialTextures(baked) ==
		std::vector<std::string>{ "Textures/base.ktx2", "Textures/nrm.ktx2", "Textures/orm.ktx2" });

	// A loose material is its authoring routes instead, one slot per channel, unrouted ones empty.
	auto loose                        = assetlib::BMaterial();
	loose.mode                        = assetlib::MaterialMode::kLoose;
	loose.pbr.routes[0].texture       = "Textures/albedo.ktx2";
	const std::vector<std::string> ch = game::materialTextures(loose);

	REQUIRE(ch.size() == assetlib::c_LooseChannelCount);
	CHECK(ch[0] == "Textures/albedo.ktx2");
	CHECK(ch[1].empty());
}

TEST_CASE("A prefetched texture is uploaded without its file being read", "[gamelib][assets]")
{
	// The whole point of a prefetch: the decode -- the expensive, pure-CPU half -- can run anywhere,
	// leaving only the upload on the render thread.
	Fixture fx("bernini_am_prefetch");

	// Never written to disk. AcquireTexture is documented to throw on a missing file (and a sibling
	// test pins that), so if this succeeds, the file was not read -- which is exactly the claim.
	constexpr auto c_Path = "Textures/never_on_disk.ktx2";

	auto image     = assetlib::ImageData();
	image.width    = 1;
	image.height   = 1;
	image.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
	image.pixels   = core::fixed_buffer<std::byte>(4);
	std::fill_n(image.pixels.data(), 4, std::byte{ 0xFF });
	image.subresources.push_back({ 0, 4, 4 });

	auto prefetch = game::TexturePrefetch();
	prefetch.emplace(c_Path, std::move(image));

	const auto tex = (*fx).AcquireTexture(c_Path, &prefetch);

	CHECK(tex.textureSlot);
	CHECK((*fx).TextureRefCount(tex) == 1);

	// Consumed, not copied: an ImageData is a whole mip chain and holding it after the upload would
	// double the peak.
	CHECK(prefetch.empty());
}

TEST_CASE("A prefetch the texture is missing from falls back to the file", "[gamelib][assets]")
{
	// A partial prefetch is a valid one -- a texture whose decode failed is simply left out of it.
	Fixture fx("bernini_am_prefetch_partial");
	WriteTexture(fx.root.path / "Textures" / "real.ktx2");

	auto prefetch = game::TexturePrefetch();  // empty

	CHECK((*fx).AcquireTexture("Textures/real.ktx2", &prefetch).textureSlot);

	// And a miss in both places is still an error, not a silent default.
	CHECK_THROWS_AS((*fx).AcquireTexture("Textures/absent.ktx2", &prefetch), std::runtime_error);
}

TEST_CASE("A material's textures come from the prefetch when it carries them", "[gamelib][assets]")
{
	Fixture fx("bernini_am_prefetch_material");

	// The material file exists; the texture it names never does.
	constexpr auto c_Texture = "Textures/only_prefetched.ktx2";
	WriteBakedMaterial(fx.root.path / "Materials" / "m.bmaterial", c_Texture);

	auto image     = assetlib::ImageData();
	image.width    = 1;
	image.height   = 1;
	image.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
	image.pixels   = core::fixed_buffer<std::byte>(4);
	std::fill_n(image.pixels.data(), 4, std::byte{ 0xFF });
	image.subresources.push_back({ 0, 4, 4 });

	auto prefetch = game::TexturePrefetch();
	prefetch.emplace(c_Texture, std::move(image));

	const bgl::MaterialHandle mat = (*fx).AcquireMaterial("Materials/m.bmaterial", &prefetch);

	REQUIRE(mat.IsValid());
	CHECK(prefetch.empty());
}
