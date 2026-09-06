#include "Windows/MaterialEditor/material_graph.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointF>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <qjsonobject.h>
#include <qlatin1stringview.h>
#include <qobject.h>
#include <qsize.h>
#include <qstringliteral.h>
#include <system_error>
#include <tuple>
#include <vector>

#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/BlendedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/HashedAlphaMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMaterialImport.h>

namespace
{
	constexpr double c_TextureNodeX   = -160.0;
	constexpr double c_TextureNodeGap = 210.0;
	constexpr double c_OutputNodeX    = 220.0;
	constexpr double c_OutputNodeY    = 40.0;

	// TextureNode's output ports, in its own order: the bundles first, then one per channel.
	constexpr unsigned int c_TextureRgba = 0;
	constexpr unsigned int c_TextureRgb  = 1;
	constexpr unsigned int c_TextureRg   = 2;
	constexpr unsigned int c_TextureR    = 3;
	constexpr unsigned int c_TextureG    = 4;
	constexpr unsigned int c_TextureB    = 5;

	// The sink's channel groups, in BMaterial::routes order.
	constexpr unsigned int c_BaseColorGroup = 0;
	constexpr unsigned int c_OrmGroup       = 1;
	constexpr unsigned int c_NormalGroup    = 2;

	/** A factor at the three decimals the sink's spin boxes hold. See docs/asset_standards.md. */
	double
	AtEditorPrecision(float factor)
	{
		constexpr auto c_Steps = 1000.0;
		return std::round(static_cast<double>(factor) * c_Steps) / c_Steps;
	}
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

/**
 * Puts a saved board in a fixed order: nodes by id, connections by the endpoints they join.
 *
 * QtNodes keeps its connections in an unordered set, so two saves of an untouched graph emit them
 * in different orders and the `.bmaterial` rewrites itself with no change in it. That costs a git
 * diff on every open-and-save, and it costs `migrate` its byte-compare -- a file that differs is
 * how it decides a file is not current.
 */
void
SortGraph(QJsonObject& graph)
{
	const auto keyOf = [](const QJsonObject& c) {
		return std::tuple(
			c["outNodeId"].toInt(),
			c["outPortIndex"].toInt(),
			c["inNodeId"].toInt(),
			c["inPortIndex"].toInt());
	};

	QJsonArray connections = graph["connections"].toArray();
	auto       sorted      = std::vector<QJsonObject>();
	sorted.reserve(static_cast<size_t>(connections.size()));
	for (const QJsonValue& value : connections) sorted.push_back(value.toObject());

	std::ranges::sort(sorted, [&](const QJsonObject& a, const QJsonObject& b) {
		return keyOf(a) < keyOf(b);
	});

	QJsonArray ordered;
	for (const QJsonObject& c : sorted) ordered.append(c);
	graph["connections"] = ordered;

	QJsonArray nodes = graph["nodes"].toArray();
	auto       byId  = std::vector<QJsonObject>();
	byId.reserve(static_cast<size_t>(nodes.size()));
	for (const QJsonValue& value : nodes) byId.push_back(value.toObject());

	std::ranges::sort(byId, [](const QJsonObject& a, const QJsonObject& b) {
		return a["id"].toInt() < b["id"].toInt();
	});

	QJsonArray orderedNodes;
	for (const QJsonObject& n : byId) orderedNodes.append(n);
	graph["nodes"] = orderedNodes;
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
MakeMaterialNodeRegistry(Renderer* renderer, TexturePreviewCache* previews)
{
	auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();

	registry->registerModel<TextureNode>(
		[renderer, previews]() { return std::make_unique<TextureNode>(renderer, previews); },
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
	registry->registerModel<HashedAlphaMaterialOutputNode>(
		[]() { return std::make_unique<HashedAlphaMaterialOutputNode>(); },
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

	material.name = name.toStdString();

	if (const MaterialOutputNode* output = model.OutputNode())
	{
		assetlib::PbrParams& pbr = material.pbr;

		pbr.baseColorFactor = output->BaseColorFactor();
		pbr.metallicFactor  = output->MetallicFactor();
		pbr.roughnessFactor = output->RoughnessFactor();

		pbr.alphaMode          = output->GetAlphaMode();
		pbr.alphaCutoff        = output->GetAlphaCutoff();
		pbr.doubleSided        = output->GetDoubleSided();
		pbr.transmissionFactor = output->GetTransmission();

		pbr.specularColorFactor = output->GetSpecularColorFactor();
		pbr.specularFactor      = output->GetSpecularFactor();

		for (unsigned int i = 0; i < assetlib::c_LooseChannelCount; ++i)
		{
			const ChannelData::Route wired = output->Route(i);

			pbr.routes[i].texture = Rebase(wired.path, dataRoot, true).toStdString();
			pbr.routes[i].channel = wired.channel;
		}
	}

	QJsonObject graph = model.save();
	RebaseGraphTextures(graph, dataRoot, true);
	SortGraph(graph);
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
	const bool hashed      = material.alphaMode == assetlib::AlphaMode::kHashed;
	// Every sink but the opaque one exposes a 4-wide base-color port; that one is 3-wide.
	const bool carriesAlpha = alphaTested || blended || hashed;

	// kHashed never arrives from an import -- glTF cannot say it -- but a graph rebuilt from a
	// material that was authored to it does.
	QString outputModel;
	if (alphaTested)
		outputModel = QStringLiteral("AlphaTestedMaterialOutput");
	else if (blended)
		outputModel = QStringLiteral("BlendedMaterialOutput");
	else if (hashed)
		outputModel = QStringLiteral("HashedAlphaMaterialOutput");
	else
		outputModel = QStringLiteral("MaterialOutput");

	const QtNodes::NodeId outputId = model.addNode(outputModel);
	model.setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(c_OutputNodeX, c_OutputNodeY));

	auto* output = model.delegateModel<MaterialOutputNode>(outputId);
	if (output == nullptr)
		return;

	// glTF specifies only G and B of the metallic-roughness texture; its red is occlusion solely by
	// the shared-ORM convention. A map of its own therefore wins ORM red, and forces the group into
	// per-channel ports so the other two can keep coming from the metallic-roughness texture. A
	// material where the two name one image is that convention holding, and keeps its single wire.
	const bool splitOrm = !maps.occlusion.isEmpty() && maps.occlusion != maps.orm;

	// The sink's own deserialization path is what carries the factors in; restoring a saved graph sets
	// them the same way. `alphaCutoff` is ignored by the opaque sink.
	auto factors            = QJsonObject();
	factors["baseColorR"]   = material.baseColorFactor.r;
	factors["baseColorG"]   = material.baseColorFactor.g;
	factors["baseColorB"]   = material.baseColorFactor.b;
	factors["baseColorA"]   = material.baseColorFactor.a;
	factors["metallic"]     = AtEditorPrecision(material.metallicFactor);
	factors["roughness"]    = AtEditorPrecision(material.roughnessFactor);
	factors["alphaCutoff"]  = AtEditorPrecision(material.alphaCutoff);
	factors["transmission"] = AtEditorPrecision(material.transmissionFactor);
	factors["doubleSided"]  = material.doubleSided;
	factors["specularR"]    = material.specularColorFactor.r;
	factors["specularG"]    = material.specularColorFactor.g;
	factors["specularB"]    = material.specularColorFactor.b;
	factors["specular"]     = AtEditorPrecision(material.specularFactor);
	factors["split"]        = QJsonArray{ false, splitOrm, false };
	output->load(factors);

	struct Wire
	{
		QString      path;
		unsigned int texturePort;
		unsigned int outputPort;
	};

	// A whole map feeds its group's wide port: glTF puts roughness in G and metallic in B, which is
	// where ORM wants them, and a normal map's Z is reconstructed in the shader, so only RG is taken.
	// Base colour
	// draws alpha only for a cutout -- the opaque sink's port is 3-wide, and routing an alpha that
	// nothing tests against is what turns a project into cutouts that cut nothing out.
	auto wires = std::vector<Wire>{
		{ maps.baseColor,
		  carriesAlpha ? c_TextureRgba : c_TextureRgb,
		  output->GroupPort(c_BaseColorGroup, 0) },
		{ maps.normal, c_TextureRg, output->GroupPort(c_NormalGroup, 0) },
	};

	if (splitOrm)
	{
		// An absent metallic-roughness texture leaves roughness and metallic unrouted, which the bake
		// fills with the ORM group's fallback -- the value that leaves the factors alone in charge.
		wires.push_back({ maps.occlusion, c_TextureR, output->GroupPort(c_OrmGroup, 0) });
		wires.push_back({ maps.orm, c_TextureG, output->GroupPort(c_OrmGroup, 1) });
		wires.push_back({ maps.orm, c_TextureB, output->GroupPort(c_OrmGroup, 2) });
	}
	else
	{
		wires.push_back({ maps.orm, c_TextureRgb, output->GroupPort(c_OrmGroup, 0) });
	}

	// One node per distinct map, so a texture feeding two channels of a split group is placed once.
	auto   nodeForPath = QHash<QString, QtNodes::NodeId>();
	double y           = 0.0;

	for (const Wire& wire : wires)
	{
		if (wire.path.isEmpty())
			continue;

		auto placed = nodeForPath.find(wire.path);
		if (placed == nodeForPath.end())
		{
			const QtNodes::NodeId textureId = model.addNode(QStringLiteral("Texture"));
			model.setNodeData(textureId, QtNodes::NodeRole::Position, QPointF(c_TextureNodeX, y));
			y += c_TextureNodeGap;

			if (auto* texture = model.delegateModel<TextureNode>(textureId))
				texture->SetTexturePath(wire.path);

			placed = nodeForPath.insert(wire.path, textureId);
		}

		model.addConnection(
			QtNodes::ConnectionId{ *placed,
		                           static_cast<QtNodes::PortIndex>(wire.texturePort),
		                           outputId,
		                           static_cast<QtNodes::PortIndex>(wire.outputPort) });
	}
}

std::optional<QPointF>
OutputCentre(MaterialGraphModel& model)
{
	const QtNodes::NodeId outputId = model.OutputNodeId();
	if (outputId == QtNodes::InvalidNodeId)
		return std::nullopt;

	const QPointF pos  = model.nodeData(outputId, QtNodes::NodeRole::Position).value<QPointF>();
	const QSize   size = model.nodeData(outputId, QtNodes::NodeRole::Size).value<QSize>();

	// A node is only measured once it has a graphics object, so a model with no scene reports -1 x -1.
	// Half of that would centre just off the node's corner, which is worse than its corner.
	if (!size.isValid())
		return pos;

	return pos + QPointF(size.width() * 0.5, size.height() * 0.5);
}
