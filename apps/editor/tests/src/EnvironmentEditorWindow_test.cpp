#include "Windows/EnvironmentEditor/EnvironmentEditorWindow.h"
#include "Windows/EnvironmentEditor/environment_editor_ui.h"

#include "StoreAt.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
	namespace fs = std::filesystem;

	// A data root holding one environment document, and nothing else: the panel reads and writes the
	// `.benv` alone, and the `.bsky` / `.benvl` it names are only ever displayed.
	struct Sandbox
	{
		fs::path root;

		Sandbox() : root(fs::temp_directory_path() / "bernini_env_editor")
		{
			fs::remove_all(root);
			fs::create_directories(root / "Authored/Environments");

			auto env          = assetlib::BEnv();
			env.name          = "forest";
			env.sky           = "Derived/Sky/forest.bsky";
			env.pbr.lighting  = "Derived/EnvLighting/forest.benvl";
			env.skyMipLevel   = 2;
			env.skyRotationY  = glm::radians(90.0f);
			env.rim.tint      = glm::vec3(1.0f, 0.5f, 0.25f);
			env.rim.intensity = 1.5f;
			env.rim.power     = 3.0f;
			SaveAt(env, Path());
		}

		~Sandbox() { fs::remove_all(root); }

		[[nodiscard]] fs::path
		Path() const
		{
			return root / "Authored/Environments" / "forest.benv";
		}

		[[nodiscard]] QString
		QtPath() const
		{
			return QString::fromStdWString(Path().wstring());
		}
	};

	// The panel stands without a graphics device -- it degrades to a placeholder where the preview
	// would be -- which is what makes everything below runnable on the CPU.
	EnvironmentEditorWindow*
	MakePanel(const Sandbox& sandbox)
	{
		auto* panel = new EnvironmentEditorWindow(nullptr, EnvironmentEditorWindowDesc());
		panel->SetDataRoot(QString::fromStdWString(sandbox.root.wstring()));
		return panel;
	}
}

TEST_CASE("The environment panel offers nothing to edit until one is open", "[environmenteditor]")
{
	const Sandbox            sandbox;
	EnvironmentEditorWindow* panel = MakePanel(sandbox);

	for (const char* name : { "EnvSkyMipLevel",
	                          "EnvSkyRotationY",
	                          "EnvOverrideExposure",
	                          "EnvRimTint",
	                          "EnvRimIntensity",
	                          "EnvRimPower",
	                          "EnvSave" })
	{
		INFO(name);
		CHECK_FALSE(panel->findChild<QWidget*>(QLatin1String(name))->isEnabled());
	}

	CHECK(panel->GetHeldOpenPaths().isEmpty());

	delete panel;
}

TEST_CASE("The environment panel shows what the document authored", "[environmenteditor]")
{
	const Sandbox            sandbox;
	EnvironmentEditorWindow* panel = MakePanel(sandbox);

	panel->OpenEnvironment(sandbox.QtPath());

	CHECK(panel->findChild<QSpinBox*>("EnvSkyMipLevel")->value() == 2);

	// Degrees in the panel, radians in the document: a person authors the first and the shader
	// reads the second.
	CHECK(
		panel->findChild<QDoubleSpinBox*>("EnvSkyRotationY")->value() ==
		Catch::Approx(90.0).margin(0.001));

	CHECK(panel->findChild<QDoubleSpinBox*>("EnvRimIntensity")->value() == Catch::Approx(1.5));
	CHECK(panel->findChild<QDoubleSpinBox*>("EnvRimPower")->value() == Catch::Approx(3.0));

	// Unset in the document, so the box is clear and the value beside it cannot be edited.
	CHECK_FALSE(panel->findChild<QCheckBox*>("EnvOverrideExposure")->isChecked());
	CHECK_FALSE(panel->findChild<QDoubleSpinBox*>("EnvExposure")->isEnabled());

	// Nothing has been edited, so there is nothing to save.
	CHECK_FALSE(panel->findChild<QPushButton*>("EnvSave")->isEnabled());

	CHECK(panel->GetHeldOpenPaths() == QStringList{ sandbox.QtPath() });

	delete panel;
}

TEST_CASE("The environment panel writes an edit back to the document", "[environmenteditor]")
{
	const Sandbox            sandbox;
	EnvironmentEditorWindow* panel = MakePanel(sandbox);

	panel->OpenEnvironment(sandbox.QtPath());

	panel->findChild<QDoubleSpinBox*>("EnvRimIntensity")->setValue(4.0);
	panel->findChild<QSpinBox*>("EnvSkyMipLevel")->setValue(0);

	auto* save = panel->findChild<QPushButton*>("EnvSave");
	CHECK(save->isEnabled());  // an edit is what arms it

	save->click();
	CHECK_FALSE(save->isEnabled());  // and saving disarms it again

	const auto written = LoadAt<assetlib::BEnv>(sandbox.Path());
	CHECK(written.rim.intensity == Catch::Approx(4.0f));
	CHECK(written.skyMipLevel == 0);

	// Everything the panel does not author is carried through untouched -- it round-trips the
	// document rather than rebuilding it.
	CHECK(written.sky == "Derived/Sky/forest.bsky");
	CHECK(written.pbr.lighting == "Derived/EnvLighting/forest.benvl");
	CHECK(written.rim.tint == glm::vec3(1.0f, 0.5f, 0.25f));

	delete panel;
}

/**
 * The exposure override is two controls for one optional: a box that says whether it is authored at
 * all, and a value that only means anything while it is.
 */
TEST_CASE("The environment panel's exposure override is authored or absent", "[environmenteditor]")
{
	const Sandbox            sandbox;
	EnvironmentEditorWindow* panel = MakePanel(sandbox);

	panel->OpenEnvironment(sandbox.QtPath());

	auto* box   = panel->findChild<QCheckBox*>("EnvOverrideExposure");
	auto* value = panel->findChild<QDoubleSpinBox*>("EnvExposure");

	box->setChecked(true);
	CHECK(value->isEnabled());
	value->setValue(0.25);
	panel->findChild<QPushButton*>("EnvSave")->click();

	auto written = LoadAt<assetlib::BEnv>(sandbox.Path());
	REQUIRE(written.pbr.exposureOverride.has_value());
	CHECK(*written.pbr.exposureOverride == Catch::Approx(0.25f));

	SECTION("clearing it puts the document back to the bake's derivation")
	{
		box->setChecked(false);
		CHECK_FALSE(value->isEnabled());
		panel->findChild<QPushButton*>("EnvSave")->click();

		written = LoadAt<assetlib::BEnv>(sandbox.Path());
		CHECK_FALSE(written.pbr.exposureOverride.has_value());
	}

	delete panel;
}

// config.json names the environment as a path of its own and the data root separately -- the second
// is what the paths *inside* the `.benv` resolve against, not a prefix for the first. Resolving one
// against the other names `<root>/<root>/...`, which opens nothing.
//
// Under the working directory, and both paths short and relative, because that is config.json's
// geometry -- `assets/Data` beside `assets/Data/Authored/...`. A path reached with `..` cannot pin
// this: the leading `..` of the second cancels the first back out, and a join with an absolute
// right-hand side yields the right-hand side, so either shape survives the bug untouched.
TEST_CASE(
	"The panel opens the environment its desc names, not one under the data root twice",
	"[environmenteditor]")
{
	// Per test process, so the shards this suite is split across cannot collide here.
	const fs::path root =
		"bernini_env_relroot_" +
		std::to_string(std::hash<std::string>{}(fs::temp_directory_path().string()));
	const fs::path document = root / "Authored/Environments/forest.benv";

	fs::remove_all(root);
	fs::create_directories(document.parent_path());

	auto env          = assetlib::BEnv();
	env.name          = "forest";
	env.skyMipLevel   = 2;
	env.rim.intensity = 1.5f;
	SaveAt(env, document);

	auto desc                      = EnvironmentEditorWindowDesc();
	desc.startupEnv.environmentMap = document.string();
	desc.startupEnv.dataRoot       = root;

	auto* panel = new EnvironmentEditorWindow(nullptr, desc);

	// Opened: the controls a document enables carry its values, and the panel holds the file.
	CHECK(panel->findChild<QSpinBox*>("EnvSkyMipLevel")->value() == 2);
	CHECK(panel->findChild<QDoubleSpinBox*>("EnvRimIntensity")->value() == Catch::Approx(1.5));

	const QStringList held = panel->GetHeldOpenPaths();
	REQUIRE(held.size() == 1);
	CHECK(
		fs::weakly_canonical(fs::path(held.front().toStdWString())) ==
		fs::weakly_canonical(document));

	delete panel;
	fs::remove_all(root);
}
