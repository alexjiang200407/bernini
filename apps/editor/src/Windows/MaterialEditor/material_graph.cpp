#include "Windows/MaterialEditor/material_graph.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointF>

#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/BlendedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"

namespace
{
	constexpr double c_TextureNodeX   = -160.0;
	constexpr double c_TextureNodeGap = 210.0;
	constexpr double c_OutputNodeX    = 220.0;
	constexpr double c_OutputNodeY    = 40.0;
}

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
	registry->registerModel<BlendedMaterialOutputNode>(
		[]() { return std::make_unique<BlendedMaterialOutputNode>(); },
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

		pbr.alphaMode   = output->AlphaMode();
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

void
BuildImportedMaterialGraph(
	MaterialGraphModel&                   model,
	const assetlib::imp::BMaterialImport& material,
	const ImportedMaterialMaps&           maps)
{
	const bool alphaTested = material.alphaMode == assetlib::AlphaMode::kMask;
	const bool blended     = material.alphaMode == assetlib::AlphaMode::kBlend;
	// A cutout and a blend sink both expose a 4-wide base-color port; the opaque one is 3-wide.
	const bool carriesAlpha = alphaTested || blended;

	const QString outputModel = alphaTested ? QStringLiteral("AlphaTestedMaterialOutput") :
	                            blended     ? QStringLiteral("BlendedMaterialOutput") :
	                                          QStringLiteral("MaterialOutput");

	const QtNodes::NodeId outputId = model.addNode(outputModel);
	model.setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(c_OutputNodeX, c_OutputNodeY));

	auto* output = model.delegateModel<MaterialOutputNode>(outputId);
	if (output == nullptr)
		return;

	// The sink's own deserialization path is what carries the factors in; restoring a saved graph sets
	// them the same way. `alphaCutoff` is ignored by the opaque sink, and `split` is absent, which
	// leaves every group collapsed to the one wide port each map wires into below.
	auto factors           = QJsonObject();
	factors["baseColorR"]  = material.baseColorFactor.r;
	factors["baseColorG"]  = material.baseColorFactor.g;
	factors["baseColorB"]  = material.baseColorFactor.b;
	factors["baseColorA"]  = material.baseColorFactor.a;
	factors["metallic"]    = material.metallicFactor;
	factors["roughness"]   = material.roughnessFactor;
	factors["alphaCutoff"] = material.alphaCutoff;
	output->load(factors);

	struct Wire
	{
		QString      path;
		unsigned int texturePort;
		unsigned int outputPort;
	};

	// Each map feeds its group's whole port: glTF packs occlusion/roughness/metallic in RGB, which is
	// the ORM order, and a normal map's Z is reconstructed in the shader, so only RG is taken. Base
	// colour draws alpha only for a cutout -- the opaque sink's port is 3-wide, and routing an alpha
	// that nothing tests against is what turns a project into cutouts that cut nothing out.
	const std::array<Wire, 3> wires = { {
		{ maps.baseColor, carriesAlpha ? 0u : 1u, 0u },
		{ maps.orm, 1u, 1u },
		{ maps.normal, 2u, 2u },
	} };

	double y = 0.0;
	for (const Wire& wire : wires)
	{
		if (wire.path.isEmpty())
			continue;

		const QtNodes::NodeId textureId = model.addNode(QStringLiteral("Texture"));
		model.setNodeData(textureId, QtNodes::NodeRole::Position, QPointF(c_TextureNodeX, y));
		y += c_TextureNodeGap;

		if (auto* texture = model.delegateModel<TextureNode>(textureId))
			texture->SetTexturePath(wire.path);

		model.addConnection(
			QtNodes::ConnectionId{ textureId,
		                           static_cast<QtNodes::PortIndex>(wire.texturePort),
		                           outputId,
		                           static_cast<QtNodes::PortIndex>(wire.outputPort) });
	}
}
