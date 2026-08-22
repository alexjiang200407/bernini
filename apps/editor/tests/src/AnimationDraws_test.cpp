#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/AnimationEditor/TimelineScrubber.h"
#include "Windows/AnimationEditor/animation_draws.h"

#include "util/MeshFixture.h"

#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

#include <QTemporaryDir>

#include <catch2/catch_approx.hpp>

// The transport glue behind the panel, pinned without a window: which mesh entries play as VAT
// and which stand static, the clip-table conversion, and the timeline's tick mapping.

namespace
{
	// One submesh: skinned means it carries joint indices.
	uint32_t
	AddEntry(assetlib::BMesh& mesh, bool skinned)
	{
		auto submesh                  = assetlib::Submesh();
		submesh.indexType             = assetlib::IndexType::kUint16;
		submesh.layout.attributeCount = 1;
		submesh.layout.attributes[0].semantic =
			skinned ? assetlib::VertexSemantic::kJoints0 : assetlib::VertexSemantic::kPosition;

		const auto first = static_cast<uint32_t>(mesh.submeshes.size());
		mesh.submeshes.push_back(submesh);
		mesh.meshes.push_back({ .firstSubmesh = first, .submeshCount = 1, .nameOffset = 0 });
		return static_cast<uint32_t>(mesh.meshes.size() - 1);
	}

	void
	AddNode(assetlib::BMesh& mesh, uint32_t meshIndex)
	{
		auto node           = assetlib::Node();
		node.localTransform = { glm::vec3(0.0f),
			                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                    glm::vec3(1.0f) };
		node.parent         = assetlib::c_InvalidIndex;
		node.firstChild     = assetlib::c_InvalidIndex;
		node.nextSibling    = assetlib::c_InvalidIndex;
		node.mesh           = meshIndex;
		node.nameOffset     = 0;
		mesh.nodes.push_back(node);
	}
}

TEST_CASE("A skinned entry plays as VAT; a static one stands beside it", "[animation]")
{
	auto mesh     = assetlib::BMesh();
	mesh.skeleton = "Skeletons/rig.bskel";

	const uint32_t skinned = AddEntry(mesh, true);
	const uint32_t prop    = AddEntry(mesh, false);
	AddNode(mesh, skinned);
	AddNode(mesh, prop);

	const auto plan = editor::PlanAnimationDraws(mesh);

	REQUIRE(plan.animated.size() == 1);
	CHECK(plan.animated[0].meshIndex == skinned);
	REQUIRE(plan.statics.size() == 1);
	CHECK(plan.statics[0].meshIndex == prop);
}

TEST_CASE("The clip table converts field for field", "[animation]")
{
	const auto infos = editor::ToClipInfos(
		std::array{ game::ClipInfo{ "walk", 24, 30.0f, 0.767f, true },
	                game::ClipInfo{ "die", 12, 15.0f, 0.733f, false } });

	REQUIRE(infos.size() == 2);
	CHECK(infos[0].name == "walk");
	CHECK(infos[0].frameCount == 24);
	CHECK(infos[0].sampleRate == 30.0f);
	CHECK(infos[0].loop);
	CHECK(infos[1].name == "die");
	CHECK_FALSE(infos[1].loop);
}

TEST_CASE("Timeline ticks round-trip the clock inside the period", "[animation]")
{
	CHECK(AnimationEditorWindow::TimelineTicks(0.0f, 2.0f, 1000) == 0);
	CHECK(AnimationEditorWindow::TimelineTicks(1.0f, 2.0f, 1000) == 500);
	CHECK(AnimationEditorWindow::TimelineTicks(2.0f, 2.0f, 1000) == 1000);
	CHECK(AnimationEditorWindow::TimelineTicks(5.0f, 2.0f, 1000) == 1000);  // clamped
	CHECK(AnimationEditorWindow::TimelineTicks(1.0f, 0.0f, 1000) == 0);     // no clips

	CHECK(AnimationEditorWindow::TimelineSeconds(500, 2.0f, 1000) == Catch::Approx(1.0f));
	CHECK(AnimationEditorWindow::TimelineSeconds(1000, 2.0f, 1000) == Catch::Approx(2.0f));
	CHECK(AnimationEditorWindow::TimelineSeconds(2000, 2.0f, 1000) == Catch::Approx(2.0f));
	CHECK(AnimationEditorWindow::TimelineSeconds(500, 0.0f, 1000) == 0.0f);

	const int ticks = AnimationEditorWindow::TimelineTicks(0.733f, 2.2f, 1000);
	CHECK(
		AnimationEditorWindow::TimelineSeconds(ticks, 2.2f, 1000) ==
		Catch::Approx(0.733f).margin(0.0023f));  // one tick of slack
}

TEST_CASE("The scrubber's press-to-tick mapping spans the groove exactly", "[animation]")
{
	// The handle's center travels [radius, width - radius]; presses outside clamp to the ends.
	CHECK(TimelineScrubber::ValueForX(0, 200, 1000) == 0);
	CHECK(TimelineScrubber::ValueForX(200, 200, 1000) == 1000);
	CHECK(TimelineScrubber::ValueForX(100, 200, 1000) == 500);
	CHECK(TimelineScrubber::ValueForX(-50, 200, 1000) == 0);
	CHECK(TimelineScrubber::ValueForX(500, 200, 1000) == 1000);

	// Degenerate widths never divide by zero.
	CHECK(TimelineScrubber::ValueForX(5, 0, 1000) == 0);
	CHECK(TimelineScrubber::ValueForX(5, 10, 1000) == 0);
}

// Whether the panel offers to bake is this, not the tier: a refusal it can answer gets the button
// and one it cannot gets a plain message. The offer used to be gated on the VAT tier, which meant a
// skinned mesh refused *for exactly this reason* was told to fix it and not offered the fix.
TEST_CASE("Only a material a bake would change is offered for baking", "[animation][source]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const auto root = std::filesystem::path(dir.path().toStdWString());
	std::filesystem::create_directories(root / "Materials");

	const auto write = [&root](const char* name, const assetlib::BMaterial& material) {
		assetlib::saveMaterial(material, root / "Materials" / name);
		return std::string("Materials/") + name;
	};

	// Routed at a texture that exists, never baked -- what a fresh import leaves behind, and the
	// state that refuses a skinned preview.
	std::ofstream(root / "Materials" / "albedo.ktx2", std::ios::binary).put('\0');

	auto unbaked                  = assetlib::BMaterial();
	unbaked.pbr.routes[0].texture = "Materials/albedo.ktx2";
	const std::string unbakedPath = write("unbaked.bmaterial", unbaked);

	// Nothing routed: a bake has nothing to composite, so offering one would be a dead end.
	const std::string emptyPath = write("empty.bmaterial", assetlib::BMaterial());

	const auto                     store = assetlib::AssetStore(root);
	const std::vector<std::string> bakeable =
		editor::BakeableMaterials(store, std::vector<std::string>{ unbakedPath, emptyPath, "" });

	REQUIRE(bakeable.size() == 1);
	CHECK(bakeable[0] == unbakedPath);

	// A material that will not load is skipped rather than thrown on: a refusal is already being
	// reported, and a second failure on top of it helps nobody.
	CHECK(
		editor::BakeableMaterials(store, std::vector<std::string>{ "Materials/gone.bmaterial" })
			.empty());
}

TEST_CASE("A load's tier-dependent steps all follow from the source", "[animation][source]")
{
	using editor::AnimationSource;
	using editor::PlanAnimationLoad;

	SECTION("the VAT tier needs a bake and frames by its box")
	{
		const auto steps = PlanAnimationLoad(AnimationSource::kVat, /*hasAnimations*/ true);
		CHECK(steps.needsFreshBake);
		CHECK(steps.framedByBake);
	}

	SECTION("the skinned tier does none of them")
	{
		// The bake is the one that matters: it is seconds of CPU skinning for a texture pair the
		// skinned path never samples, and it would also make the preview need a bakeable material.
		// It is a requirement, not an action -- no load bakes; the panel offers and the user takes it.
		const auto steps = PlanAnimationLoad(AnimationSource::kSkinned, /*hasAnimations*/ true);
		CHECK_FALSE(steps.needsFreshBake);

		// It measures its own box instead. That box culls the geom as well as framing the camera,
		// so reading it off a bake the skinned tier never made would hide the mesh, not just
		// mis-aim the camera.
		CHECK_FALSE(steps.framedByBake);
	}

	SECTION("with no clip file, neither tier does anything")
	{
		// Nothing to play: the mesh stands in its bind pose as static geometry, so a bake is
		// meaningless -- including on the VAT tier, which would otherwise need one.
		for (const AnimationSource source : { AnimationSource::kSkinned, AnimationSource::kVat })
		{
			const auto steps = PlanAnimationLoad(source, /*hasAnimations*/ false);
			CHECK_FALSE(steps.needsFreshBake);
			CHECK_FALSE(steps.framedByBake);
		}
	}
}

TEST_CASE("The panel's acquires are prepared before the render thread sees them", "[animation]")
{
	// A real .bmesh on disk, because a prepare reads one -- that is the point of it.
	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const auto root = std::filesystem::path(dir.path().toStdWString());

	auto mesh     = assetlib::BMesh();
	mesh.skeleton = "Skeletons/rig.bskel";

	const uint32_t skinned = editor::test::AddTriangleMesh(mesh, 1, true);
	const uint32_t prop    = editor::test::AddTriangleMesh(mesh, 1, false);
	editor::test::AddMeshNode(mesh, skinned, 0.0f);
	editor::test::AddMeshNode(mesh, prop, 3.0f);

	std::filesystem::create_directories(root / "Meshes");
	assetlib::save(mesh, root / "Meshes" / "rig.bmesh");

	const auto store = assetlib::AssetStore(root);
	const auto plan  = editor::PlanAnimationDraws(mesh);
	REQUIRE(plan.animated.size() == 1);
	REQUIRE(plan.statics.size() == 1);

	SECTION("with no clip set anywhere, every placement is prepared as bind-pose static geometry")
	{
		const auto prepares = editor::PrepareAnimationDraws(
			store,
			"Meshes/rig.bmesh",
			{},
			editor::AnimationSource::kVat,
			plan);

		REQUIRE(prepares.size() == 2);
		for (const editor::PreparedAnimationDraw& draw : prepares)
		{
			CHECK_FALSE(draw.animated);
			CHECK(draw.prepared.tier == game::MeshTier::kStatic);
			CHECK(draw.refusal.empty());

			// The cook is in hand, which is the whole claim: the commit has only an upload left.
			CHECK(draw.prepared.meshIndex == draw.placement.meshIndex);
		}

		// Statics first, then the animated ones, which is the order the commit places them in.
		CHECK(prepares[0].placement.meshIndex == prop);
		CHECK(prepares[1].placement.meshIndex == skinned);
	}

	SECTION("a tier that refuses a placement drops it to static, carrying the reason")
	{
		// No .banim on disk, so both animated doors refuse. The panel must still stand the mesh up.
		const auto prepares = editor::PrepareAnimationDraws(
			store,
			"Meshes/rig.bmesh",
			"Animations/missing.banim",
			editor::AnimationSource::kSkinned,
			plan);

		REQUIRE(prepares.size() == 2);

		const editor::PreparedAnimationDraw& animated = prepares[1];
		CHECK(animated.placement.meshIndex == skinned);
		CHECK_FALSE(animated.animated);
		CHECK(animated.prepared.tier == game::MeshTier::kStatic);
		CHECK_FALSE(animated.refusal.empty());
	}
}
