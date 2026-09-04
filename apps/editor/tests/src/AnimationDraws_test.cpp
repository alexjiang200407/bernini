#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/AnimationEditor/animation_draws.h"

#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

#include <QTemporaryDir>

#include "StoreAt.h"
#include <catch2/catch_approx.hpp>

// The transport glue behind the panel, pinned without a window: which mesh entries animate and
// which stand static, the clip-table conversion, the timeline's tick mapping, and which pose source
// each tier-selector entry names.

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

TEST_CASE("A skinned entry animates; a static one stands beside it", "[animation]")
{
	auto mesh     = assetlib::BMesh();
	mesh.skeleton = "Derived/Skeletons/rig.bskel";

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
	CHECK(Scrubber::ValueForX(0, 200, 0, 1000) == 0);
	CHECK(Scrubber::ValueForX(200, 200, 0, 1000) == 1000);
	CHECK(Scrubber::ValueForX(100, 200, 0, 1000) == 500);
	CHECK(Scrubber::ValueForX(-50, 200, 0, 1000) == 0);
	CHECK(Scrubber::ValueForX(500, 200, 0, 1000) == 1000);

	// Degenerate widths never divide by zero.
	CHECK(Scrubber::ValueForX(5, 0, 0, 1000) == 0);
	CHECK(Scrubber::ValueForX(5, 10, 0, 1000) == 0);

	// A range with a floor below zero -- the ground slope's -30..30 -- maps from that floor and
	// not from nothing, so its middle is the middle of the groove and not one end of it.
	CHECK(Scrubber::ValueForX(0, 200, -30, 30) == -30);
	CHECK(Scrubber::ValueForX(100, 200, -30, 30) == 0);
	CHECK(Scrubber::ValueForX(200, 200, -30, 30) == 30);

	// An empty range answers its floor rather than dividing by its width.
	CHECK(Scrubber::ValueForX(100, 200, 7, 7) == 7);
}

// Whether the panel offers to bake is this: a refusal it can answer gets the button, one it cannot
// gets a plain message. It is about the mesh's materials and never about which pose source is
// selected -- the two sources draw the same upload, so a refusal reaches both alike.
TEST_CASE("Only a material a bake would change is offered for baking", "[animation][source]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const auto root = std::filesystem::path(dir.path().toStdWString());
	std::filesystem::create_directories(root / "Authored/Materials");

	const auto write = [&root](const char* name, const assetlib::BMaterial& material) {
		SaveAt(material, root / "Authored/Materials" / name);
		return std::string("Authored/Materials/") + name;
	};

	// Routed at a texture that exists, never baked -- what a fresh import leaves behind, and the
	// state that refuses a skinned preview.
	std::ofstream(root / "Authored/Materials" / "albedo.ktx2", std::ios::binary).put('\0');

	auto unbaked                  = assetlib::BMaterial();
	unbaked.pbr.routes[0].texture = "Authored/Materials/albedo.ktx2";
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
		editor::BakeableMaterials(
			store,
			std::vector<std::string>{ "Authored/Materials/gone.bmaterial" })
			.empty());
}

TEST_CASE("The tier selector's entries are the two pose sources", "[animation][source]")
{
	// Pinned because nothing on screen can catch it: both sources draw the same picture at a whole
	// frame (that is what the crowd tier is *for*), so a selector wired to the wrong one, or to
	// nothing at all, looks exactly like a correct one. The panel's own Eyes check can only tell
	// that the entry draws, not that it drew through the tier it names.
	SECTION("the first entry is the per-instance pose, the second the rig's shared table")
	{
		CHECK(AnimationEditorWindow::TierSourceAt(0) == bgl::PoseSource::kPerInstance);
		CHECK(AnimationEditorWindow::TierSourceAt(1) == bgl::PoseSource::kBoneAnimTable);
	}

	SECTION("a source maps back to the entry that selects it")
	{
		for (const bgl::PoseSource source :
		     { bgl::PoseSource::kPerInstance, bgl::PoseSource::kBoneAnimTable })
		{
			CHECK(
				AnimationEditorWindow::TierSourceAt(AnimationEditorWindow::TierIndexFor(source)) ==
				source);
		}
	}

	SECTION("an index the combo can never hold reads as the default tier")
	{
		// QComboBox::activated cannot deliver these, but the panel's own guard drops a negative
		// index rather than relying on that -- so the mapping answers with the hero tier, which is
		// what an instance gets when nothing sets a source.
		CHECK(AnimationEditorWindow::TierSourceAt(-1) == bgl::PoseSource::kPerInstance);
		CHECK(AnimationEditorWindow::TierIndexFor(bgl::PoseSource::kPerInstance) == 0);
	}
}
