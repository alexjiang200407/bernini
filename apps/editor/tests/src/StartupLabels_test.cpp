#include "Startup/startup_labels.h"

#include <catch2/catch_test_macros.hpp>
#include <qstringliteral.h>

#include "util/QtSupport.h"  // IWYU pragma: keep
#include <assetlib/progress.h>

// The one part of the startup screen a test can reach: what each report turns into. The screen
// itself needs eyes -- this pins the rule that decides what those eyes read.

using namespace editor;

TEST_CASE("A rebuild report names the phase and the file, not the whole key", "[startup]")
{
	auto event = assetlib::ProgressEvent();

	// The directory is the same on every line of a rebuild, and it is the part that pushes the
	// name that actually changes off the end of the screen.
	event.phase   = assetlib::ProgressPhase::kRegenerating;
	event.subject = "Derived/Meshes/unit.bmesh";
	CHECK(startup::RebuildLabel(event) == QStringLiteral("Rebuilding unit.bmesh"));

	event.phase   = assetlib::ProgressPhase::kExtractingTextures;
	event.subject = "Authored/Meshes/apples.glb";
	CHECK(startup::RebuildLabel(event) == QStringLiteral("Extracting textures apples.glb"));

	event.phase   = assetlib::ProgressPhase::kBakingMaterials;
	event.subject = "Authored/Materials/red.bmaterial";
	CHECK(startup::RebuildLabel(event) == QStringLiteral("Baking material red.bmaterial"));

	event.phase   = assetlib::ProgressPhase::kResaving;
	event.subject = "Derived/Animations/unit.banim";
	CHECK(startup::RebuildLabel(event) == QStringLiteral("Updating unit.banim"));
}

TEST_CASE("A rebuild step about no one file still says what it is doing", "[startup]")
{
	auto event    = assetlib::ProgressEvent();
	event.phase   = assetlib::ProgressPhase::kScanning;
	event.subject = {};

	// A phase boundary carries no subject, and a label that read "Checking " with nothing after it
	// looks like a truncated string rather than a step.
	CHECK(startup::RebuildLabel(event) == QStringLiteral("Checking..."));
}

TEST_CASE("A key with no directory is shown whole", "[startup]")
{
	auto event    = assetlib::ProgressEvent();
	event.phase   = assetlib::ProgressPhase::kResaving;
	event.subject = "loose.bmaterial";

	CHECK(startup::RebuildLabel(event) == QStringLiteral("Updating loose.bmaterial"));
}
