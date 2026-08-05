#include "EditorConfig.h"

#include <QDebug>

#include <core/settings/Settings.h>

namespace
{
	/**
	 * A count, rejected when it falls below `minimum` or will not fit the field it is read into.
	 *
	 * `minimum` is 1 almost everywhere, and zero is the value worth catching: it parses and it reads
	 * as deliberate. What it then does differs -- a graphics pool of zero trips an assertion inside
	 * device creation that names nothing the user wrote, while a scene pool is silently clamped up to
	 * one and simply behaves badly. Both are worth a warning; only the first is worth preventing.
	 *
	 * `scene.initialSubmeshes` passes 0, because bgl reads that as "one per meshlet".
	 */
	uint32_t
	ReadCount(
		const core::SettingsAccessor& value,
		uint32_t                      fallback,
		const char*                   key,
		int64_t                       minimum = 1) noexcept
	{
		if (value.IsNull())
			return fallback;

		const int64_t read = value.GetOrDefault(static_cast<int64_t>(fallback));
		if (read < minimum || read > std::numeric_limits<uint32_t>::max())
		{
			qWarning(
				"EditorConfig: '%s' is %lld, which is not a usable count; using %u",
				key,
				static_cast<long long>(read),
				fallback);
			return fallback;
		}

		return static_cast<uint32_t>(read);
	}

	EditorConfig::Environment
	ReadEnvironment(const core::SettingsAccessor& section)
	{
		auto environment           = EditorConfig::Environment();
		environment.environmentMap = section["environmentMap"].GetOrDefault(std::string());
		environment.dataRoot       = section["dataRoot"].GetOrDefault(std::string());

		if (const core::SettingsAccessor exposure = section["exposure"]; !exposure.IsNull())
			environment.exposureOverride = exposure.GetOrDefault(1.0f);

		return environment;
	}
}

EditorConfig
EditorConfig::Parse(const core::Settings& settings)
{
	auto config = EditorConfig();

	config.startupProject = settings["startupProject"].GetOrDefault(std::string());

	const core::SettingsAccessor graphics = settings["graphics"];

	config.graphics.enableDebugLayer = graphics["enableDebugLayer"].GetOrDefault(false);
	config.graphics.enableGPUValidationLayer =
		graphics["enableGPUBasedValidation"].GetOrDefault(false);
	config.graphics.enablePixDebug = graphics["enablePixDebug"].GetOrDefault(false);
	config.graphics.strictError    = graphics["strictError"].GetOrDefault(false);
	config.graphics.logLevel       = static_cast<bgl::GraphicsOptions::LogLevel>(
		graphics["logLevel"].GetOrDefault(static_cast<int>(config.graphics.logLevel)));

	config.graphics.maxCbvSrvUavs =
		ReadCount(graphics["maxCbvSrvUavs"], config.graphics.maxCbvSrvUavs, "maxCbvSrvUavs");
	config.graphics.maxBuffers =
		ReadCount(graphics["maxBuffers"], config.graphics.maxBuffers, "maxBuffers");
	config.graphics.maxSrvs = ReadCount(graphics["maxSrvs"], config.graphics.maxSrvs, "maxSrvs");
	config.graphics.maxRtvs = ReadCount(graphics["maxRtvs"], config.graphics.maxRtvs, "maxRtvs");
	config.graphics.maxDsvs = ReadCount(graphics["maxDsvs"], config.graphics.maxDsvs, "maxDsvs");
	config.graphics.maxTextures =
		ReadCount(graphics["maxTextures"], config.graphics.maxTextures, "maxTextures");

	// A shader cache is a directory or nothing; the flag chooses between them.
	if (graphics["enableShaderCache"].GetOrDefault(true))
		config.graphics.shaderCacheDir = "shadercache";

	const core::SettingsAccessor scene = settings["scene"];

	config.scene.initialGeom =
		ReadCount(scene["initialGeom"], config.scene.initialGeom, "initialGeom");
	config.scene.initialMeshlets =
		ReadCount(scene["initialMeshlets"], config.scene.initialMeshlets, "initialMeshlets");
	config.scene.initialSubmeshes =
		ReadCount(scene["initialSubmeshes"], config.scene.initialSubmeshes, "initialSubmeshes", 0);
	config.scene.initialVertexBufferByteSize = ReadCount(
		scene["initialVertexBufferByteSize"],
		config.scene.initialVertexBufferByteSize,
		"initialVertexBufferByteSize");
	config.scene.initialIndices =
		ReadCount(scene["initialIndices"], config.scene.initialIndices, "initialIndices");
	config.scene.initialPbrMaterials = ReadCount(
		scene["initialPbrMaterials"],
		config.scene.initialPbrMaterials,
		"initialPbrMaterials");
	config.scene.initialLoosePbrMaterials = ReadCount(
		scene["initialLoosePbrMaterials"],
		config.scene.initialLoosePbrMaterials,
		"initialLoosePbrMaterials");

	const core::SettingsAccessor levelEditor = settings["levelEditor"];

	config.levelEditor.initialInstances = ReadCount(
		levelEditor["initialInstances"],
		config.levelEditor.initialInstances,
		"levelEditor.initialInstances");
	config.levelEditor.temporalAA =
		levelEditor["temporalAA"].GetOrDefault(config.levelEditor.temporalAA);

	const core::SettingsAccessor materialEditor = settings["materialEditor"];

	config.materialEditor.initialPreviewInstances = ReadCount(
		materialEditor["initialPreviewInstances"],
		config.materialEditor.initialPreviewInstances,
		"materialEditor.initialPreviewInstances");
	config.materialEditor.temporalAA =
		materialEditor["temporalAA"].GetOrDefault(config.materialEditor.temporalAA);
	config.materialEditor.environment = ReadEnvironment(materialEditor);

	const core::SettingsAccessor thumbnails = settings["thumbnails"];

	config.thumbnails.initialInstances = ReadCount(
		thumbnails["initialInstances"],
		config.thumbnails.initialInstances,
		"thumbnails.initialInstances");
	config.thumbnails.dimension =
		ReadCount(thumbnails["dimension"], config.thumbnails.dimension, "thumbnails.dimension");
	config.thumbnails.environment = ReadEnvironment(thumbnails);

	return config;
}
