#include "asset_describe.h"
#include <assetlib/bmesh.h>
#include <assetlib/container_info.h>

#include <assetlib/skeleton.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <core/file/LooseFileSystem.h>

using namespace assetlib;

namespace
{
	BMaterial
	RoutedMaterial()
	{
		BMaterial material;
		material.name                 = "skin";
		material.pbr.metallicFactor   = 0.0f;
		material.pbr.roughnessFactor  = 0.75f;
		material.pbr.baseColorTexture = "Textures/basecolor_dead.ktx2";

		material.pbr.routes[0] = { "textures_src/skin.ktx2", 0 };
		material.pbr.routes[1] = { "textures_src/skin.ktx2", 1 };
		material.pbr.routes[5] = { "textures_src/mask.ktx2", 3 };  // roughness <- mask.a
		return material;
	}

	Skeleton
	TwoBoneRig()
	{
		Skeleton skeleton;

		Bone hips{};
		hips.bindPose    = { glm::vec3(0.0f, 1.0f, 0.0f),
			                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                 glm::vec3(1.0f) };
		hips.inverseBind = glm::mat4(1.0f);
		hips.parent      = c_InvalidIndex;
		hips.nameOffset  = skeleton.stringPool.add("hips");
		skeleton.bones.push_back(hips);

		Bone spine{};
		spine.bindPose    = { glm::vec3(0.0f, 2.0f, 0.0f),
			                  glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                  glm::vec3(1.0f) };
		spine.inverseBind = glm::mat4(1.0f);
		spine.parent      = 0;
		spine.nameOffset  = skeleton.stringPool.add("spine");
		skeleton.bones.push_back(spine);

		return skeleton;
	}

	AnimationSet
	WalkClip(const Skeleton& skeleton)
	{
		AnimationSet animations;
		animations.skeleton          = "Animations/rig.bskel";
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		animations.skeletonSignature = skeletonSignature(skeleton);

		AnimationClip walk{};
		walk.nameOffset      = animations.stringPool.add("walk");
		walk.firstSample     = 0;
		walk.frameCount      = 2;
		walk.duration        = 0.5f;
		walk.sampleRate      = 30.0f;
		walk.loop            = 1;
		walk.rootMotion      = glm::vec3(0.0f, 0.0f, 1.5f);
		walk.locomotionSpeed = 3.0f;
		animations.clips.push_back(walk);

		const Transform rest{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		animations.samples.assign(walk.frameCount * animations.boneCount, rest);

		return animations;
	}
}

// The point of describe() is to answer "what is in this file" without hand-decoding it, so the
// properties a reader actually goes looking for have to survive into the text: the routing table's
// channel selectors and the unrouted channels.
TEST_CASE("describe(BMaterial) reports the routing table", "[describe]")
{
	const std::string text = describe(RoutedMaterial());

	CHECK(text.find("skin") != std::string::npos);

	// A route names its source and the channel it draws from -- routes[5] is roughness <- mask.a, and
	// a swizzle that silently printed the wrong letter would make the dump worse than useless.
	CHECK(text.find("baseColor.r") != std::string::npos);
	CHECK(text.find("textures_src/skin.ktx2 [r]") != std::string::npos);
	CHECK(text.find("textures_src/skin.ktx2 [g]") != std::string::npos);
	CHECK(text.find("textures_src/mask.ktx2 [a]") != std::string::npos);

	// The channels left unrouted are exactly the ones that fall back to a default texture at render
	// time, which is the single most common cause of a material looking wrong. They must be visible.
	CHECK(text.find("metallic        (unrouted)") != std::string::npos);
	CHECK(text.find("normal.x        (unrouted)") != std::string::npos);

	CHECK(text.find("Textures/basecolor_dead.ktx2") != std::string::npos);
	CHECK(text.find("orm             (none)") != std::string::npos);
}

// With a data root, each routed source is stat'd and compared against the stamp taken at bake time.
TEST_CASE("describe(BMaterial) reports bake staleness against the data root", "[describe]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_describe";
	std::filesystem::create_directories(root / "textures_src");

	const core::file::LooseFileSystem files(root);

	const auto source = root / "textures_src" / "skin.ktx2";
	{
		std::ofstream out(source, std::ios::binary);
		out << "some source bytes";
	}

	BMaterial material;
	material.pbr.routes[0] = { "textures_src/skin.ktx2", 0 };

	SECTION("a source that has drifted from its stamp is STALE")
	{
		// The stamp is left zeroed: this route was never baked, so it cannot match the live source.
		const std::string text = describe(material, &files);
		CHECK(text.find("STALE") != std::string::npos);
	}

	SECTION("a source matching its stamp is up to date")
	{
		material.pbr.routeStamps[0]   = stampOf(source);
		material.pbr.baseColorTexture = "Textures/baked.ktx2";

		// The map has to be there as well as named: a triplet entry pointing at nothing is stale.
		std::filesystem::create_directories(root / "Textures");
		{
			std::ofstream out(root / "Textures" / "baked.ktx2", std::ios::binary);
			out << "baked bytes";
		}

		const std::string text = describe(material, &files);
		CHECK(text.find("up to date") != std::string::npos);
		CHECK(text.find("STALE") == std::string::npos);
	}

	SECTION("a missing source is called out rather than reported as a mismatch")
	{
		material.pbr.routes[0] = { "textures_src/gone.ktx2", 0 };

		const std::string text = describe(material, &files);
		CHECK(text.find("source is missing") != std::string::npos);
	}

	std::filesystem::remove_all(root);
}

// A submesh whose material index is out of range draws with the renderer's default material. That is
// invisible in the raw bytes and easy to misread as "material 0", so the dump has to name it.
TEST_CASE("describe(BMesh) resolves each submesh's material path", "[describe]")
{
	BMesh mesh;
	REQUIRE(mesh.stringPool.add("head") == 1);
	mesh.materials = { "Materials/head.bmaterial" };
	mesh.meshes.push_back(Mesh{ .firstSubmesh = 0, .submeshCount = 2, .nameOffset = 0 });

	Submesh named{};
	named.nameOffset  = 1;  // "head"
	named.material    = 0;
	named.vertexCount = 12;
	named.indexCount  = 36;
	named.indexType   = IndexType::kUint16;
	mesh.submeshes.push_back(named);

	Submesh dangling{};
	dangling.material = 7;  // no such entry in `materials`
	mesh.submeshes.push_back(dangling);

	const std::string text = describe(mesh);

	CHECK(text.find("'head'") != std::string::npos);
	CHECK(text.find("[0] Materials/head.bmaterial") != std::string::npos);
	CHECK(text.find("[7] (out of range -- no material)") != std::string::npos);

	// Brief mode keeps the material table but drops the per-submesh listing.
	const std::string brief = describe(mesh, /*verbose*/ false);
	CHECK(brief.find("Materials/head.bmaterial") != std::string::npos);
	CHECK(brief.find("'head'") == std::string::npos);
}

// A .benv holds no pixels at all, so the only thing worth reading out of it is what it names -- and
// whether those names resolve. A dangling reference renders as an environment that silently loses its
// sky or its lighting, which is exactly the case a dump has to make visible.
// A .bskel is a bone array addressed by bare index, so the dump exists to make the two things an
// index cannot show -- who a bone's parent is, and which rig this is -- readable.
TEST_CASE("describe(Skeleton) names each bone and its parent", "[describe][skeleton]")
{
	const Skeleton    skeleton = TwoBoneRig();
	const std::string text     = describe(skeleton);

	CHECK(text.find("bones        2") != std::string::npos);
	CHECK(text.find("'hips'") != std::string::npos);
	CHECK(text.find("'spine'") != std::string::npos);

	// A root has no parent index to print, and printing c_InvalidIndex as a number would read as a
	// bone that exists.
	CHECK(text.find("(root)") != std::string::npos);
	CHECK(text.find("parent 0") != std::string::npos);

	CHECK(text.find(std::format("{:016x}", skeletonSignature(skeleton))) != std::string::npos);
}

TEST_CASE("describe(AnimationSet) reports each clip's timing and motion", "[describe][animation]")
{
	const Skeleton     skeleton   = TwoBoneRig();
	const AnimationSet animations = WalkClip(skeleton);

	SECTION("without a skeleton to check against")
	{
		const std::string text = describe(animations);

		CHECK(text.find("Animations/rig.bskel") != std::string::npos);
		CHECK(text.find("clips        1") != std::string::npos);
		CHECK(text.find("'walk'") != std::string::npos);
		CHECK(text.find("2 frames at 30 Hz") != std::string::npos);
		CHECK(text.find("looping") != std::string::npos);

		// Nothing was passed to check the binding against, so the line must be absent rather than
		// guessing an answer.
		CHECK(text.find("binding") == std::string::npos);
	}

	SECTION("against the skeleton it was cooked from")
	{
		const std::string text = describe(animations, &skeleton);
		CHECK(text.find("matches the skeleton") != std::string::npos);
	}

	// The case the signature exists for: a bone inserted since the clips were cooked. Nothing about
	// the samples themselves changes, so this line is the only place it surfaces.
	SECTION("against a rig that has drifted since")
	{
		Skeleton reordered            = skeleton;
		reordered.bones[1].nameOffset = reordered.stringPool.add("chest");

		const std::string text = describe(animations, &reordered);
		CHECK(text.find("DOES NOT MATCH") != std::string::npos);
	}

	SECTION("a clip that does not loop says nothing about looping")
	{
		AnimationSet once  = animations;
		once.clips[0].loop = 0;

		CHECK(describe(once).find("looping") == std::string::npos);
	}
}

// A joint index resolves against a bone array or against nothing, and which one is invisible in the
// geometry -- so the mesh dump has to say when a skinned mesh names no rig.
TEST_CASE("describe(BMesh) reports the skeleton a skinned mesh names", "[describe][skeleton]")
{
	BMesh mesh;
	mesh.meshes.push_back(Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	Submesh submesh{};
	submesh.indexType                     = IndexType::kUint16;
	submesh.layout.attributeCount         = 1;
	submesh.layout.attributes[0].semantic = VertexSemantic::kJoints0;
	submesh.layout.attributes[0].format   = VertexFormat::kUint16x4;
	mesh.submeshes.push_back(submesh);

	REQUIRE(isSkinned(mesh));

	SECTION("named")
	{
		mesh.skeleton = "Meshes/rig.bskel";
		CHECK(describe(mesh).find("skeleton     Meshes/rig.bskel") != std::string::npos);
	}

	SECTION("carrying joints but naming none")
	{
		CHECK(describe(mesh).find("(SKINNED, but names none)") != std::string::npos);
	}

	SECTION("a static mesh naming a rig says the rig is unused")
	{
		BMesh attachment;
		attachment.meshes.push_back(Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		attachment.submeshes.push_back(Submesh{});
		attachment.skeleton = "Meshes/rig.bskel";

		REQUIRE_FALSE(isSkinned(attachment));
		CHECK(describe(attachment).find("unused: no submesh carries joints") != std::string::npos);
	}
}

TEST_CASE("describe(BEnv) reports whether the files it names are there", "[describe]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_describe_benv";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "Sky");

	const core::file::LooseFileSystem files(root);

	{
		std::ofstream out(root / "Sky" / "forest.bsky", std::ios::binary);
		out << "x";
	}

	BEnv env;
	env.name     = "forest";
	env.sky      = "Sky/forest.bsky";
	env.lighting = "EnvLighting/forest.benvl";  // never written

	// Without a root there is nothing to resolve against, so neither is judged.
	const std::string bare = describe(env);
	CHECK(bare.find("forest") != std::string::npos);
	CHECK(bare.find("Sky/forest.bsky") != std::string::npos);
	CHECK(bare.find("(missing)") == std::string::npos);

	const std::string text = describe(env, &files);
	CHECK(text.find("Sky/forest.bsky\n") != std::string::npos);  // present: unannotated
	CHECK(text.find("EnvLighting/forest.benvl (missing)") != std::string::npos);

	// An unset half is not the same as a missing one, and must not read as a broken reference.
	BEnv skyless;
	skyless.lighting            = "EnvLighting/forest.benvl";
	const std::string unsetText = describe(skyless, &files);
	CHECK(unsetText.find("sky               (unset)") != std::string::npos);

	std::filesystem::remove_all(root);
}

// The sky and the lighting carry EnvMapRoutes, which stale exactly the way a material's channel
// routes do -- so the same question ("is what I am about to render still what was baked?") has to be
// answerable here too.
TEST_CASE("describe(BSky) and describe(BEnvLighting) report bake staleness", "[describe]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_describe_env";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "textures_src");

	const core::file::LooseFileSystem files(root);

	std::filesystem::create_directories(root / "Textures");

	const auto source = root / "textures_src" / "forest.ktx2";
	const auto baked  = root / "Textures" / "forest_sky_ab12.ktx2";
	const auto write  = [](const std::filesystem::path& path, std::string_view bytes) {
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	};
	write(source, "aaaa");

	// The map has to be on disk, not merely named: a route claiming one that is gone is stale, so a
	// fixture that never wrote it would be describing a stale bake while calling itself current.
	write(baked, "bbbb");

	BSky sky;
	sky.name       = "forest";
	sky.sky.source = "textures_src/forest.ktx2";
	sky.sky.baked  = "Textures/forest_sky_ab12.ktx2";
	sky.sky.stamp  = stampOf(source);

	SECTION("a sky reports its route and a current bake")
	{
		const std::string text = describe(sky, &files);

		CHECK(text.find("bsky 'forest'") != std::string::npos);
		CHECK(text.find("textures_src/forest.ktx2") != std::string::npos);
		CHECK(text.find("Textures/forest_sky_ab12.ktx2") != std::string::npos);
		CHECK(text.find("source up to date") != std::string::npos);
		CHECK(text.find("STALE") == std::string::npos);
	}

	// The case the stricter rule exists for: nothing about the source moved, but what was baked from
	// it is gone, so the route names a file there is nothing to sample.
	SECTION("a sky whose baked map is gone says so and reads as stale")
	{
		std::filesystem::remove(baked);

		const std::string text = describe(sky, &files);
		CHECK(text.find("baked map is missing") != std::string::npos);
		CHECK(text.find("bake              STALE") != std::string::npos);
	}

	SECTION("a sky whose source moved on reads as stale")
	{
		write(source, "aaaaaaaa");  // different size

		const std::string text = describe(sky, &files);
		CHECK(text.find("STALE") != std::string::npos);
	}

	SECTION("a sky whose source is gone says so rather than comparing stamps")
	{
		std::filesystem::remove(source);

		const std::string text = describe(sky, &files);
		CHECK(text.find("source is missing") != std::string::npos);
	}

	BEnvLighting lighting;
	lighting.name              = "forest";
	lighting.exposure          = 1.25f;
	lighting.prefilter.source  = "textures_src/forest.ktx2";
	lighting.prefilter.baked   = "Textures/forest_prefilter.ktx2";
	lighting.prefilter.stamp   = stampOf(source);
	lighting.irradiance.source = "textures_src/forest.ktx2";
	lighting.irradiance.baked  = "Textures/forest_irradiance.ktx2";
	lighting.irradiance.stamp  = stampOf(source);

	SECTION("a lighting names both halves and its exposure")
	{
		const std::string text = describe(lighting, &files);

		CHECK(text.find("benvl 'forest'") != std::string::npos);
		CHECK(text.find("exposure          1.25") != std::string::npos);
		CHECK(text.find("prefilter") != std::string::npos);
		CHECK(text.find("irradiance") != std::string::npos);
		CHECK(text.find("Textures/forest_prefilter.ktx2") != std::string::npos);
		CHECK(text.find("Textures/forest_irradiance.ktx2") != std::string::npos);
	}

	// The pair is one verdict: they convolve the same radiance, so one drifting makes both suspect.
	SECTION("one half drifting makes the whole lighting stale")
	{
		lighting.irradiance.stamp = SourceStamp{ 1, 1 };

		const std::string text = describe(lighting, &files);
		CHECK(text.find("bake              STALE") != std::string::npos);
	}

	// Without a root nothing is stat'd, so the recorded stamp is all that can be reported -- and no
	// verdict may be printed, because none was measured.
	SECTION("no data root reports the recorded stamp and no verdict")
	{
		const std::string text = describe(sky);

		CHECK(text.find("baked from") != std::string::npos);
		CHECK(text.find("up to date") == std::string::npos);
		CHECK(text.find("STALE") == std::string::npos);
	}

	std::filesystem::remove_all(root);
}
