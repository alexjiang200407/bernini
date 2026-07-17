#include "Windows/MaterialEditor/material_graph.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"

QString
Rebase(const QString& path, const std::filesystem::path& dir, bool toRelative)
{
	if (path.isEmpty() || dir.empty())
		return path;

	std::error_code       ec;
	const auto            source = std::filesystem::path(path.toStdWString());
	std::filesystem::path result =
		toRelative ? std::filesystem::relative(source, dir, ec) : (dir / source);
	if (ec || result.empty())
		return path;

	result = toRelative ? result : std::filesystem::weakly_canonical(result, ec);
	if (ec)
		return path;

	return QString::fromStdWString(result.generic_wstring());
}

void
RebaseGraphTextures(QJsonObject& graph, const std::filesystem::path& dir, bool toRelative)
{
	QJsonArray nodes = graph["nodes"].toArray();
	for (QJsonValueRef nodeValue : nodes)
	{
		QJsonObject node     = nodeValue.toObject();
		QJsonObject internal = node["internal-data"].toObject();

		if (internal["model-name"].toString() != QLatin1String("Texture"))
			continue;

		internal["texture"]   = Rebase(internal["texture"].toString(), dir, toRelative);
		node["internal-data"] = internal;
		nodeValue             = node;
	}
	graph["nodes"] = nodes;
}

std::shared_ptr<QtNodes::NodeDelegateModelRegistry>
MakeMaterialNodeRegistry(bgl::IScene* scene, TexturePreviewCache* previews)
{
	auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();

	registry->registerModel<TextureNode>(
		[scene, previews]() { return std::make_unique<TextureNode>(scene, previews); },
		"Input");

	// Registered so the graph can create one by name and restore one from a saved graph -- but hidden
	// from the context menu, because a sink is switched, not added.
	registry->registerModel<MaterialOutputNode>(
		[]() { return std::make_unique<MaterialOutputNode>(); },
		QLatin1String(c_OutputCategory));
	registry->registerModel<AlphaTestedMaterialOutputNode>(
		[]() { return std::make_unique<AlphaTestedMaterialOutputNode>(); },
		QLatin1String(c_OutputCategory));

	return registry;
}

assetlib::BMaterial
CompileMaterial(
	MaterialGraphModel&          model,
	const QString&               name,
	const std::filesystem::path& dataRoot)
{
	auto material = assetlib::BMaterial();

	material.shadingModel = assetlib::ShadingModel::kPbr;

	// The graph authors per-channel routes, so a material that has never been baked is loose.
	material.mode = assetlib::MaterialMode::kLoose;
	material.name = name.toStdString();

	if (const MaterialOutputNode* output = model.OutputNode())
	{
		assetlib::PbrParams& pbr = material.pbr;

		pbr.baseColorFactor = output->BaseColorFactor();
		pbr.metallicFactor  = output->MetallicFactor();
		pbr.roughnessFactor = output->RoughnessFactor();

		pbr.alphaMode =
			output->IsAlphaTested() ? assetlib::AlphaMode::kMask : assetlib::AlphaMode::kOpaque;
		pbr.alphaCutoff = output->AlphaCutoff();

		for (unsigned int i = 0; i < assetlib::c_LooseChannelCount; ++i)
		{
			const ChannelData::Route wired = output->Route(i);

			pbr.routes[i].texture = Rebase(wired.path, dataRoot, true).toStdString();
			pbr.routes[i].channel = wired.channel;
		}
	}

	QJsonObject graph = model.save();
	RebaseGraphTextures(graph, dataRoot, true);
	material.editorGraph = QJsonDocument(graph).toJson(QJsonDocument::Compact).toStdString();

	return material;
}
