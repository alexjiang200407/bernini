#include "EditorConfig.h"

#include <QFile>
#include <QTemporaryDir>

#include <core/settings/Settings.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
	/** A `config.json` written for one case, so a test says what it is testing in full. */
	struct Config
	{
		QTemporaryDir temp;

		[[nodiscard]] EditorConfig
		Parse(const char* json) const
		{
			REQUIRE(temp.isValid());

			const QString path = temp.filePath("config.json");

			QFile file(path);
			REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
			file.write(json);
			file.close();

			const core::Settings settings((std::filesystem::path(path.toStdWString())));
			return EditorConfig::Parse(settings);
		}
	};
}

TEST_CASE("An empty config is every default", "[config]")
{
	// The defaults live in EditorConfig's member initialisers, so this compares the parse against
	// the struct rather than against a second copy of the numbers.
	const Config       config;
	const EditorConfig parsed   = config.Parse("{}");
	const auto         expected = EditorConfig();

	REQUIRE(parsed.startupProject.empty());
	REQUIRE(parsed.scene.initialGeom == expected.scene.initialGeom);
	REQUIRE(parsed.graphics.maxSrvs == expected.graphics.maxSrvs);
	REQUIRE(parsed.levelEditor.initialInstances == expected.levelEditor.initialInstances);
	REQUIRE(parsed.levelEditor.temporalAA == expected.levelEditor.temporalAA);
	REQUIRE(parsed.thumbnails.dimension == expected.thumbnails.dimension);
	REQUIRE(parsed.materialEditor.environment.environmentMap.empty());
}

TEST_CASE("The scene defaults are the editor's, not the library's floor", "[config]")
{
	// Literal numbers on purpose: these are what MainWindow's constructor used to hardcode, and
	// lifting the parse out of it is exactly where they could be lost. bgl::SceneDesc defaults every
	// pool to 1 -- the right floor for a library that cannot know what it will hold, and unusable for
	// an editor that opens a project on launch.
	const Config       config;
	const EditorConfig parsed = config.Parse("{}");

	REQUIRE(parsed.scene.initialGeom == 256);
	REQUIRE(parsed.scene.initialMeshlets == 32768);
	REQUIRE(parsed.scene.initialIndices == 2000000);
	REQUIRE(parsed.scene.initialSubmeshes == 512);
	REQUIRE(parsed.scene.initialVertexBufferByteSize == 33554432);
	REQUIRE(parsed.scene.initialPbrMaterials == 256);
	REQUIRE(parsed.scene.initialLoosePbrMaterials == 256);
}

TEST_CASE("A section that is absent falls back whole", "[config]")
{
	// Not key by key: a config naming only `graphics` must still start the editor.
	const Config       config;
	const EditorConfig parsed   = config.Parse(R"({ "graphics": { "maxSrvs": 64 } })");
	const auto         expected = EditorConfig();

	REQUIRE(parsed.graphics.maxSrvs == 64);
	REQUIRE(parsed.scene.initialGeom == expected.scene.initialGeom);
	REQUIRE(parsed.thumbnails.initialInstances == expected.thumbnails.initialInstances);
}

TEST_CASE("What is written is what is read", "[config]")
{
	const Config       config;
	const EditorConfig parsed = config.Parse(R"({
		"startupProject": "~/projects/kirk.berniniproject",
		"graphics": { "enableDebugLayer": true, "maxRtvs": 8 },
		"scene": { "initialGeom": 4 },
		"levelEditor": { "initialInstances": 7, "temporalAA": false },
		"materialEditor": { "initialPreviewInstances": 3, "environmentMap": "e.benv", "dataRoot": "d" },
		"thumbnails": { "dimension": 128 }
	})");

	REQUIRE(parsed.startupProject == "~/projects/kirk.berniniproject");
	REQUIRE(parsed.graphics.enableDebugLayer);
	REQUIRE(parsed.graphics.maxRtvs == 8);
	REQUIRE(parsed.scene.initialGeom == 4);
	REQUIRE(parsed.levelEditor.initialInstances == 7);
	REQUIRE_FALSE(parsed.levelEditor.temporalAA);
	REQUIRE(parsed.materialEditor.initialPreviewInstances == 3);
	REQUIRE(parsed.materialEditor.environment.environmentMap == "e.benv");
	REQUIRE(parsed.materialEditor.environment.dataRoot == std::filesystem::path("d"));
	REQUIRE(parsed.thumbnails.dimension == 128);
}

TEST_CASE("A count that cannot be honoured falls back rather than being passed on", "[config]")
{
	// Zero is the value worth catching: it parses, it reads as deliberate, and passed on it fails
	// much later inside device or scene creation, as an error naming nothing the user wrote.
	const Config       config;
	const EditorConfig parsed   = config.Parse(R"({
		"graphics": { "maxSrvs": 0 },
		"scene": { "initialGeom": -1 },
		"levelEditor": { "initialInstances": 0 },
		"thumbnails": { "dimension": 0 }
	})");
	const auto         expected = EditorConfig();

	REQUIRE(parsed.graphics.maxSrvs == expected.graphics.maxSrvs);
	REQUIRE(parsed.scene.initialGeom == expected.scene.initialGeom);
	REQUIRE(parsed.levelEditor.initialInstances == expected.levelEditor.initialInstances);
	REQUIRE(parsed.thumbnails.dimension == expected.thumbnails.dimension);
}

TEST_CASE("A submesh pool of zero is a choice, not a mistake", "[config]")
{
	// The one count zero is meaningful for: bgl reads it as "one per meshlet" (Scene::InitBuffers).
	// The guard that rejects every other zero must not reach this one.
	const Config       config;
	const EditorConfig parsed = config.Parse(R"({ "scene": { "initialSubmeshes": 0 } })");

	REQUIRE(parsed.scene.initialSubmeshes == 0);

	// A negative is still nonsense, here as anywhere.
	REQUIRE(
		config.Parse(R"({ "scene": { "initialSubmeshes": -1 } })").scene.initialSubmeshes ==
		EditorConfig().scene.initialSubmeshes);
}

TEST_CASE("A count too large for the field it fills falls back too", "[config]")
{
	const Config       config;
	const EditorConfig parsed   = config.Parse(R"({ "scene": { "initialIndices": 99999999999 } })");
	const auto         expected = EditorConfig();

	REQUIRE(parsed.scene.initialIndices == expected.scene.initialIndices);
}

TEST_CASE("An absent exposure leaves the environment's own", "[config]")
{
	// The `.benv` carries the exposure derived from its maps, which is the right one. Config only
	// overrules it deliberately, so absent and zero have to stay distinguishable.
	const Config config;

	REQUIRE_FALSE(config.Parse(R"({ "thumbnails": { "dimension": 64 } })")
	                  .thumbnails.environment.exposureOverride.has_value());

	const std::optional<float> overridden = config.Parse(R"({ "thumbnails": { "exposure": 0.0 } })")
	                                            .thumbnails.environment.exposureOverride;

	REQUIRE(overridden.has_value());
	REQUIRE(*overridden == 0.0f);
}

TEST_CASE("The shader cache is a directory or nothing", "[config]")
{
	const Config config;

	REQUIRE_FALSE(config.Parse("{}").graphics.shaderCacheDir.empty());
	REQUIRE(config.Parse(R"({ "graphics": { "enableShaderCache": false } })")
	            .graphics.shaderCacheDir.empty());
}
