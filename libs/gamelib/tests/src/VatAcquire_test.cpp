#include <gamelib/AssetManager.h>
#include <gamelib/vat_freshness.h>

#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"

#include <assetlib/banim_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/image_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <core/file/file.h>

// The end-to-end gate the plan reserves for gamelib: .gltf-shaped data synthesized to disk, baked
// through the assetlib API on demand, loaded, drawn, and asserted on pixels -- the one test that
// crosses every layer at the seam that actually joins them.

namespace
{
	namespace fs = std::filesystem;

	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = true;
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		return opts;
	}

	struct DataRoot
	{
		fs::path path;

		explicit DataRoot(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path);
		}
		~DataRoot() { fs::remove_all(path); }
	};

	// A 1x1 white .ktx2 so the material has something real to sample.
	void
	WriteTexture(const fs::path& path)
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

		fs::create_directories(path.parent_path());
		assetlib::writeKTX2(image, path, false, assetlib::Ktx2Compression::kNone);
	}

	void
	WriteMaterial(const fs::path& path, assetlib::AlphaMode alphaMode)
	{
		auto material                 = assetlib::BMaterial();
		material.pbr.baseColorTexture = "Textures/white.ktx2";
		material.pbr.alphaMode        = alphaMode;

		fs::create_directories(path.parent_path());
		assetlib::saveMaterial(material, path);
	}

	/**
	 * The rig on disk: one bone, a 4-vertex quad welded to it (one meshlet), and a 2-frame "slide"
	 * clip translating the bone +1 X per frame -- so the pose at frame f is the quad on
	 * [f - 1, f + 1], readable off the screen. Writes Meshes/rig.bmesh, Skeletons/rig.bskel,
	 * Animations/rig.banim and the material; deliberately NO .bvat -- producing one is the
	 * manager's job.
	 */
	void
	WriteRig(const fs::path& dataRoot, assetlib::AlphaMode alphaMode = assetlib::AlphaMode::kOpaque)
	{
		auto skeleton = assetlib::Skeleton();

		auto root       = assetlib::Bone();
		root.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		root.parent     = assetlib::c_InvalidIndex;
		root.nameOffset = skeleton.stringPool.add("root");
		skeleton.bones.push_back(root);

		const auto binds              = assetlib::bindPoseModelTransforms(skeleton);
		skeleton.bones[0].inverseBind = glm::inverse(binds[0]);

		auto animations              = assetlib::AnimationSet();
		animations.skeleton          = "Skeletons/rig.bskel";
		animations.skeletonSignature = assetlib::skeletonSignature(skeleton);
		animations.boneCount         = 1;

		auto slide        = assetlib::AnimationClip();
		slide.nameOffset  = animations.stringPool.add("slide");
		slide.firstSample = 0;
		slide.frameCount  = 2;
		slide.sampleRate  = 30.0f;
		slide.duration    = 1.0f / 30.0f;
		animations.clips.push_back(slide);

		for (uint32_t frame = 0; frame < 2; ++frame)
		{
			animations.samples.push_back(
				{ glm::vec3(static_cast<float>(frame), 0.0f, 0.0f),
			      glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			      glm::vec3(1.0f) });
		}

		auto mesh = assetlib::BMesh();

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 4;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                              assetlib::VertexFormat::kFloat32x3,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              24 };
		submesh.layout.attributes[3]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              32 };
		submesh.layout.stride         = 40;

		// Corner order: bottom-left, bottom-right, top-left, top-right (CCW triangles below).
		const std::array<glm::vec3, 4> corners = { {
			{ -1.0f, -1.0f, 0.0f },
			{ 1.0f, -1.0f, 0.0f },
			{ -1.0f, 1.0f, 0.0f },
			{ 1.0f, 1.0f, 0.0f },
		} };

		for (const glm::vec3& corner : corners)
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			const glm::vec3               normal  = { 0.0f, 0.0f, 1.0f };
			const std::array<uint16_t, 4> joints  = { { 0, 0, 0, 0 } };
			const std::array<uint16_t, 4> weights = { { 65535, 0, 0, 0 } };

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &corner, sizeof(corner));
			std::memcpy(at + 12, &normal, sizeof(normal));
			std::memcpy(at + 24, joints.data(), sizeof(joints));
			std::memcpy(at + 32, weights.data(), sizeof(weights));
			++submesh.vertexCount;
		}

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexOffset   = 0;
		meshlet.triangleOffset = 0;
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingCenter = glm::vec3(0.0f);
		meshlet.boundingRadius = 2.0f;
		mesh.meshlets.push_back(meshlet);

		for (const uint32_t v : { 0u, 1u, 2u, 3u }) mesh.meshletVertices.push_back(v);
		for (const uint8_t t : { uint8_t(0),
		                         uint8_t(1),
		                         uint8_t(2),  // tri 0
		                         uint8_t(2),
		                         uint8_t(1),
		                         uint8_t(3) })
			mesh.meshletTriangles.push_back(t);

		submesh.firstMeshlet = 0;
		submesh.meshletCount = 1;
		submesh.material     = 0;
		submesh.aabbMin      = glm::vec3(-1.0f);
		submesh.aabbMax      = glm::vec3(1.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		mesh.materials.push_back("Materials/skin.bmaterial");
		mesh.skeleton = "Skeletons/rig.bskel";

		fs::create_directories(dataRoot / "Meshes");
		fs::create_directories(dataRoot / "Skeletons");
		fs::create_directories(dataRoot / "Animations");
		assetlib::save(mesh, dataRoot / "Meshes/rig.bmesh");
		assetlib::saveSkeleton(skeleton, dataRoot / "Skeletons/rig.bskel");
		assetlib::saveAnimations(animations, dataRoot / "Animations/rig.banim");

		WriteTexture(dataRoot / "Textures/white.ktx2");
		WriteMaterial(dataRoot / "Materials/skin.bmaterial", alphaMode);
	}

	/**
	 * A clip set for the same rig, written to `banimRel`: one clip `name` of `frameCount` frames,
	 * the bone at `frame * strideX` -- so frame f puts the quad on [f * strideX - 1, f * strideX + 1].
	 */
	void
	WriteClips(
		const fs::path&  dataRoot,
		const fs::path&  banimRel,
		std::string_view name,
		float            strideX,
		uint32_t         frameCount)
	{
		const auto skeleton = assetlib::loadSkeleton(dataRoot / "Skeletons/rig.bskel");

		auto animations              = assetlib::AnimationSet();
		animations.skeleton          = "Skeletons/rig.bskel";
		animations.skeletonSignature = assetlib::skeletonSignature(skeleton);
		animations.boneCount         = 1;

		auto clip        = assetlib::AnimationClip();
		clip.nameOffset  = animations.stringPool.add(name);
		clip.firstSample = 0;
		clip.frameCount  = frameCount;
		clip.sampleRate  = 30.0f;
		clip.duration    = static_cast<float>(frameCount - 1) / 30.0f;
		animations.clips.push_back(clip);

		for (uint32_t frame = 0; frame < frameCount; ++frame)
		{
			animations.samples.push_back(
				{ glm::vec3(static_cast<float>(frame) * strideX, 0.0f, 0.0f),
			      glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			      glm::vec3(1.0f) });
		}

		assetlib::saveAnimations(animations, dataRoot / banimRel);
	}

	// ~52 px per world unit at 10 units under a 60-degree, 800x600 projection.
	float
	LumaAtWorldX(const char* png, float worldX)
	{
		const int px = static_cast<int>(std::lround(400.0f + 51.96f * worldX));
		return bgl::test::MeanColor(png, px - 6, 294, 12, 12).Luma();
	}
}

TEST_CASE("A rig with no .bvat on disk is baked, loaded and drawn", "[vat][render]")
{
	DataRoot root("bernini_vat_acquire");
	WriteRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto assets = game::AssetManager(scene, root.path);

	const auto vat = assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig.banim");

	// Bake-on-demand's receipt: the derived product now sits beside its mesh, named for the pair.
	CHECK(fs::exists(root.path / assetlib::vatPathFor("Meshes/rig.bmesh", "Animations/rig.banim")));

	REQUIRE(vat.clips.size() == 1);
	CHECK(vat.clips[0].name == "slide");
	CHECK(vat.clips[0].frameCount == 2);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	SECTION("frame 1's pose comes off the screen where the clip put it")
	{
		const auto instance = assets.CreateVatInstance(
			view,
			vat.geom,
			glm::mat4(1.0f),
			bgl::VatInstanceDesc{ 0, 1.0f, 0.0f });

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_acquire_frame1.got.png";
		gfx->ScreenshotPng(target, png);

		// Frame 1 translates the bone +1 X: the quad sits on [0, 2].
		CHECK(LumaAtWorldX(png, 1.5f) > 0.05f);
		CHECK(LumaAtWorldX(png, -0.5f) < 0.01f);

		// Teardown in dependency order, then a clean frame with nothing left referencing the
		// freed VAT ranges or the released texture pair.
		assets.DestroyInstance(view, instance);
		CHECK(assets.GeomRefCount(vat.geom) == 1);

		assets.ReleaseGeom(vat.geom);
		CHECK(assets.GeomRefCount(vat.geom) == 0);

		gfx->DrawFrame(target, job);
	}

	SECTION("the source files can die under a live geom: frames read the GPU, not the disk")
	{
		const auto instance = assets.CreateVatInstance(
			view,
			vat.geom,
			glm::mat4(1.0f),
			bgl::VatInstanceDesc{ 0, 1.0f, 0.0f });

		// The editor's held-open guard refuses this; a file manager cannot be refused.
		fs::remove(root.path / "Animations/rig.banim");
		fs::remove(root.path / assetlib::vatPathFor("Meshes/rig.bmesh", "Animations/rig.banim"));

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_acquire_deleted.got.png";
		gfx->ScreenshotPng(target, png);

		// Frame 1's pose is still on screen: nothing reads the files after the acquire.
		CHECK(LumaAtWorldX(png, 1.5f) > 0.05f);
		CHECK(LumaAtWorldX(png, -0.5f) < 0.01f);

		// Released to zero, a re-acquire has nothing to bake from and says so.
		assets.DestroyInstance(view, instance);
		assets.ReleaseGeom(vat.geom);
		CHECK(assets.GeomRefCount(vat.geom) == 0);
		CHECK_THROWS_AS(
			assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig.banim"),
			std::runtime_error);
	}

	SECTION("a second acquire shares the upload; release unwinds it fully")
	{
		const auto again = assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig.banim");
		CHECK(again.geom.handle.index == vat.geom.handle.index);
		CHECK(assets.GeomRefCount(vat.geom) == 2);
		REQUIRE(again.clips.size() == 1);
		CHECK(again.clips[0].name == "slide");

		assets.ReleaseGeom(again.geom);
		assets.ReleaseGeom(vat.geom);
		CHECK(assets.GeomRefCount(vat.geom) == 0);
	}

	SECTION("a stale .bvat is re-baked on the next acquire")
	{
		assets.ReleaseGeom(vat.geom);

		// Re-author the clip set: the same rig, but the bone now slides +2 X per frame, over three
		// frames. What the stamp sees is the rewritten .banim's contents, so no timestamp has to be
		// forced for the bake to notice.
		const auto bvat =
			root.path / assetlib::vatPathFor("Meshes/rig.bmesh", "Animations/rig.banim");
		const auto original = fs::last_write_time(bvat);

		WriteClips(root.path, "Animations/rig.banim", "slide", 2.0f, 3);

		const auto rebaked = assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig.banim");

		// The re-authored frame 1 puts the quad on [1, 3] -- a pose the stale bake never held
		// (its frame 1 was [0, 2]).
		assets.CreateVatInstance(
			view,
			rebaked.geom,
			glm::mat4(1.0f),
			bgl::VatInstanceDesc{ 0, 1.0f, 0.0f });

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_acquire_rebake.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(LumaAtWorldX(png, 2.5f) > 0.05f);
		CHECK(LumaAtWorldX(png, 1.5f) > 0.05f);
		CHECK(LumaAtWorldX(png, 0.5f) < 0.01f);

		// Parenthesized so Catch2 sees one bool: it cannot stringify file_time_type's duration.
		CHECK((fs::last_write_time(bvat) != original));
	}

	SECTION("a live geom refuses a different .banim rather than returning the wrong clips")
	{
		WriteClips(root.path, "Animations/rig_march.banim", "march", 2.0f, 3);

		CHECK_THROWS_AS(
			assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig_march.banim"),
			std::runtime_error);

		// The refusal took nothing: the live geom still counts only its original acquire.
		CHECK(assets.GeomRefCount(vat.geom) == 1);
		assets.ReleaseGeom(vat.geom);
	}

	SECTION("naming a different .banim re-bakes: the container is stale by path alone")
	{
		assets.ReleaseGeom(vat.geom);

		// A second clip file for the same rig; nothing the first bake was stamped against moves.
		WriteClips(root.path, "Animations/rig_march.banim", "march", 2.0f, 3);

		const auto march = assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig_march.banim");
		REQUIRE(march.clips.size() == 1);
		CHECK(march.clips[0].name == "march");
		CHECK(march.clips[0].frameCount == 3);
		assets.ReleaseGeom(march.geom);
	}
}

TEST_CASE("A VAT acquire that cannot stand leaves nothing behind", "[vat]")
{
	DataRoot root("bernini_vat_acquire_refuse");

	// A cutout material: the VAT pipeline has no masked variant, so the acquire must refuse --
	// after it has already baked and taken its textures and material, which is the unwind under
	// test.
	WriteRig(root.path, assetlib::AlphaMode::kMask);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());

	auto scene = gfx->CreateScene(bgl::SceneDesc());

	auto assets = game::AssetManager(scene, root.path);

	CHECK_THROWS_AS(
		assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig.banim"),
		bgl::SceneError);

	// The unwind gave the material reference back: acquiring it now must count 1, not 2 -- a
	// leaked reference from the failed acquire is exactly what this would catch.
	const auto material = assets.AcquireMaterial("Materials/skin.bmaterial");
	CHECK(assets.MaterialRefCount(material) == 1);
	assets.ReleaseMaterial(material);
}

TEST_CASE("EnsureVatBaked owns the freshness rule", "[vat]")
{
	DataRoot root("bernini_vat_ensure");
	WriteRig(root.path);
	const auto bvat = root.path / assetlib::vatPathFor("Meshes/rig.bmesh", "Animations/rig.banim");

	// Missing: baked in place, recording what it was baked from.
	const auto first = game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
	REQUIRE(fs::exists(bvat));
	CHECK(first.animations == "Animations/rig.banim");
	REQUIRE(first.clips.size() == 1);
	CHECK(first.stringPool.at(first.clips[0].nameOffset) == "slide");

	// Fresh: returned from disk, not rewritten.
	const auto written = fs::last_write_time(bvat);
	(void)game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
	CHECK((fs::last_write_time(bvat) == written));

	// A different clip file is its own bake file: the first one is left standing untouched.
	WriteClips(root.path, "Animations/rig_march.banim", "march", 2.0f, 3);
	const auto march =
		game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig_march.banim");
	CHECK(march.animations == "Animations/rig_march.banim");
	REQUIRE(march.clips.size() == 1);
	CHECK(march.stringPool.at(march.clips[0].nameOffset) == "march");
	CHECK(
		fs::exists(
			root.path / assetlib::vatPathFor("Meshes/rig.bmesh", "Animations/rig_march.banim")));
	CHECK((fs::last_write_time(bvat) == written));

	// And back: the first file is still fresh, so the switch costs a load, not a bake.
	const auto back = game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
	CHECK(back.animations == "Animations/rig.banim");
	CHECK((fs::last_write_time(bvat) == written));

	// A moved input stamp re-bakes through the same door: the re-authored .banim holds different
	// bytes, which is the whole of what the stamp compares.
	WriteClips(root.path, "Animations/rig.banim", "slide", 1.0f, 4);

	const auto restamped =
		game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
	REQUIRE(restamped.clips.size() == 1);
	CHECK(restamped.clips[0].frameCount == 4);
}

// A `.bvat` written before a container major bump does not parse. It is wholly derived and
// git-ignored, so the load must fall through to a bake -- the alternative is that raising a major
// makes every project that ever baked one unopenable until something sweeps them.
TEST_CASE("A .bvat that cannot be read is re-baked, not thrown from", "[vat]")
{
	DataRoot root("bernini_vat_unreadable");
	WriteRig(root.path);
	const auto bvat = root.path / assetlib::vatPathFor("Meshes/rig.bmesh", "Animations/rig.banim");

	(void)game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
	REQUIRE(fs::exists(bvat));

	SECTION("a stale container major")
	{
		// The major sits right after the magic, and every reader checks it before anything else.
		auto bytes = core::file::read_file_bytes(bvat.string());
		REQUIRE(bytes.size() > 6);
		bytes[4] = std::byte{ 0 };
		bytes[5] = std::byte{ 0 };
		std::ofstream(bvat, std::ios::binary | std::ios::trunc)
			.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));

		const auto rebaked =
			game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
		REQUIRE(rebaked.clips.size() == 1);
		CHECK(rebaked.stringPool.at(rebaked.clips[0].nameOffset) == "slide");
	}

	SECTION("a truncated file")
	{
		std::ofstream(bvat, std::ios::binary | std::ios::trunc) << "not a bvat";

		const auto rebaked =
			game::EnsureVatBaked(root.path, "Meshes/rig.bmesh", "Animations/rig.banim");
		REQUIRE(rebaked.clips.size() == 1);
		CHECK(rebaked.stringPool.at(rebaked.clips[0].nameOffset) == "slide");
	}
}
