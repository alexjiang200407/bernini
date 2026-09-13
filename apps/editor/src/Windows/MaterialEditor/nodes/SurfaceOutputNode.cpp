#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/SurfaceTextureData.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>

#include <QColor>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPushButton>
#include <QSignalBlocker>
#include <algorithm>
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <bgl/TextureAssetHandle.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <glm/vec4.hpp>
#include <iterator>
#include <memory>
#include <qlatin1stringview.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <string>
#include <utility>

namespace
{
	// The saved graph's words for AlphaMode, indexed by the enum. The graph is the editor's own
	// blob; matching the document's words is convenience, not contract. The user-facing labels
	// live with the panel that shows them (material_editor_ui, ADR-9).
	constexpr const char* c_AlphaModeNames[] = { "opaque", "mask", "blend", "hashed" };

	// A fifth AlphaMode must extend the table, or a mode would save as nothing and load as opaque.
	static_assert(
		std::size(c_AlphaModeNames) == static_cast<size_t>(assetlib::AlphaMode::kHashed) + 1);

	const char*
	KindWord(bgl::SurfaceTextureKind kind)
	{
		switch (kind)
		{
		case bgl::SurfaceTextureKind::kData:
			return "Data";
		case bgl::SurfaceTextureKind::kNormal:
			return "Normal";
		case bgl::SurfaceTextureKind::kCoverage:
			return "Coverage";
		case bgl::SurfaceTextureKind::kColor:
			break;
		}
		return "Color";
	}

	QDoubleSpinBox*
	MakeValueSpin(QWidget* parent, double value)
	{
		auto* spin = new QDoubleSpinBox(parent);

		// A surface value is any float -- a power of 8 is as normal as a factor of 0.5 -- so the
		// range is not the factors' [0, 1].
		spin->setRange(-1.0e6, 1.0e6);
		spin->setSingleStep(0.05);
		spin->setDecimals(3);
		spin->setValue(value);
		return spin;
	}
}

SurfaceOutputNode::SurfaceOutputNode(bgl::SurfaceType surface) : m_Surface(std::move(surface))
{
	m_Values.reserve(m_Surface.params.values.size());
	for (const bgl::SurfaceValue& value : m_Surface.params.values)
		m_Values.push_back(value.defaultValue);

	m_Bound.resize(m_Surface.params.textures.size());
	m_Routes.resize(m_Surface.params.textures.size());
}

QString
SurfaceOutputNode::caption() const
{
	return QStringLiteral("%1 Surface Output").arg(QString::fromStdString(m_Surface.name));
}

QString
SurfaceOutputNode::name() const
{
	return ModelNameFor(m_Surface.name);
}

QString
SurfaceOutputNode::ModelNameFor(const std::string& surfaceName)
{
	return QStringLiteral("SurfaceOutput:%1").arg(QString::fromStdString(surfaceName));
}

unsigned int
SurfaceOutputNode::nPorts(QtNodes::PortType portType) const
{
	if (portType != QtNodes::PortType::In)
		return 0u;

	auto count = 0u;
	for (const bgl::SurfaceTexture& texture : m_Surface.params.textures)
		count += 1u + (texture.kind == bgl::SurfaceTextureKind::kData ?
		                   static_cast<unsigned int>(assetlib::c_SurfaceSlotChannelCount) :
		                   0u);
	return count;
}

SurfaceOutputNode::PortRef
SurfaceOutputNode::ResolvePort(QtNodes::PortIndex port) const
{
	auto remaining = port;
	for (size_t slot = 0; slot < m_Surface.params.textures.size(); ++slot)
	{
		if (remaining == 0)
			return { slot, true, 0 };
		--remaining;

		if (m_Surface.params.textures[slot].kind != bgl::SurfaceTextureKind::kData)
			continue;

		if (remaining < QtNodes::PortIndex(assetlib::c_SurfaceSlotChannelCount))
			return { slot, false, static_cast<uint32_t>(remaining) };
		remaining -= QtNodes::PortIndex(assetlib::c_SurfaceSlotChannelCount);
	}
	return { m_Surface.params.textures.size(), true, 0 };
}

unsigned int
SurfaceOutputNode::WholePortFor(size_t slot) const
{
	auto port = 0u;
	for (size_t i = 0; i < slot && i < m_Surface.params.textures.size(); ++i)
		port += 1u + (m_Surface.params.textures[i].kind == bgl::SurfaceTextureKind::kData ?
		                  static_cast<unsigned int>(assetlib::c_SurfaceSlotChannelCount) :
		                  0u);
	return port;
}

unsigned int
SurfaceOutputNode::ChannelPortFor(size_t slot, uint32_t component) const
{
	return WholePortFor(slot) + 1u + component;
}

QtNodes::NodeDataType
SurfaceOutputNode::dataType(QtNodes::PortType, QtNodes::PortIndex port) const
{
	return ResolvePort(port).whole ? SurfaceTextureData::Type() : ChannelData::Type(1);
}

void
SurfaceOutputNode::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex port)
{
	const PortRef ref = ResolvePort(port);
	if (ref.slot >= m_Bound.size())
		return;

	// QtNodes pushes a null payload when a wire is removed, so this covers connect and disconnect.
	if (ref.whole)
		m_Bound[ref.slot] = std::dynamic_pointer_cast<SurfaceTextureData>(data);
	else
		m_Routes[ref.slot][ref.component] = std::dynamic_pointer_cast<ChannelData>(data);

	Q_EMIT Changed();
}

bool
SurfaceOutputNode::SlotIsRouted(size_t slot) const
{
	if (slot >= m_Routes.size())
		return false;

	return std::ranges::any_of(m_Routes[slot], [](const std::shared_ptr<ChannelData>& channel) {
		return channel != nullptr;
	});
}

bool
SurfaceOutputNode::PortAccepts(QtNodes::PortIndex port) const
{
	const PortRef ref = ResolvePort(port);
	if (ref.slot >= m_Bound.size())
		return false;

	if (ref.whole)
		return !SlotIsRouted(ref.slot);
	return m_Bound[ref.slot] == nullptr;
}

ChannelData::Route
SurfaceOutputNode::RouteFor(size_t slot, uint32_t component) const
{
	if (slot >= m_Routes.size() || component >= m_Routes[slot].size())
		return {};

	const std::shared_ptr<ChannelData>& channel = m_Routes[slot][component];
	return channel != nullptr ? channel->At(0) : ChannelData::Route{};
}

QString
SurfaceOutputNode::portCaption(QtNodes::PortType, QtNodes::PortIndex port) const
{
	const PortRef ref = ResolvePort(port);
	if (ref.slot >= m_Surface.params.textures.size())
		return {};

	const bgl::SurfaceTexture& texture = m_Surface.params.textures[ref.slot];
	if (ref.whole)
		return QStringLiteral("%1 (%2)").arg(
			QString::fromStdString(texture.name),
			QLatin1String(KindWord(texture.kind)));

	constexpr const char* c_ChannelLetters[] = { "r", "g", "b", "a" };
	return QStringLiteral("%1.%2").arg(
		QString::fromStdString(texture.name),
		QLatin1String(c_ChannelLetters[ref.component]));
}

QWidget*
SurfaceOutputNode::embeddedWidget()
{
	if (m_Widget != nullptr)
		return m_Widget;

	m_Widget   = new QWidget();
	auto* form = new QFormLayout(m_Widget);
	form->setContentsMargins(4, 4, 4, 4);

	m_Spins.resize(m_Surface.params.values.size());
	m_Swatches.assign(m_Surface.params.values.size(), nullptr);
	for (size_t i = 0; i < m_Surface.params.values.size(); ++i)
	{
		const bgl::SurfaceValue& value      = m_Surface.params.values[i];
		const uint32_t           components = bgl::SurfaceValueComponents(value.type);

		auto* field = new QWidget(m_Widget);
		auto* row   = new QHBoxLayout(field);
		row->setContentsMargins(0, 0, 0, 0);

		for (uint32_t c = 0; c < components; ++c)
		{
			QDoubleSpinBox* spin =
				MakeValueSpin(field, static_cast<double>(m_Values[i][static_cast<int>(c)]));
			row->addWidget(spin);
			m_Spins[i].push_back(spin);

			connect(spin, &QDoubleSpinBox::valueChanged, this, [this, i, c](double edited) {
				m_Values[i][static_cast<int>(c)] = static_cast<float>(edited);
				RefreshSwatch(i);
				Q_EMIT Changed();
			});
		}

		// The spins stay: a colour is still any float, and a value past [0, 1] -- an emissive
		// chroma over one -- is one a picker cannot say.
		if (value.isColor)
		{
			auto* swatch = new QPushButton(field);
			swatch->setToolTip(QStringLiteral("Pick the colour"));
			row->addWidget(swatch);
			m_Swatches[i] = swatch;
			RefreshSwatch(i);

			// Queued, as every sink's picker is: the click arrives while the proxy widget is
			// dispatching the mouse event, and QColorDialog::getColor spins a nested event loop.
			connect(
				swatch,
				&QPushButton::clicked,
				this,
				[this, i]() { PickColor(i); },
				Qt::QueuedConnection);
		}

		form->addRow(QString::fromStdString(value.name), field);
	}

	SyncWidgets();
	WatchEmbeddedWidget(m_Widget);
	return m_Widget;
}

void
SurfaceOutputNode::SetAlphaMode(assetlib::AlphaMode mode)
{
	if (m_AlphaMode == mode)
		return;
	m_AlphaMode = mode;
	Q_EMIT Changed();
}

void
SurfaceOutputNode::SetAlphaCutoff(float cutoff)
{
	if (m_AlphaCutoff == cutoff)
		return;
	m_AlphaCutoff = cutoff;
	Q_EMIT Changed();
}

void
SurfaceOutputNode::SetDoubleSided(bool doubleSided)
{
	if (m_DoubleSided == doubleSided)
		return;
	m_DoubleSided = doubleSided;
	Q_EMIT Changed();
}

void
SurfaceOutputNode::SyncWidgets()
{
	if (m_Widget == nullptr)
		return;

	for (size_t i = 0; i < m_Spins.size(); ++i)
	{
		for (size_t c = 0; c < m_Spins[i].size(); ++c)
		{
			const QSignalBlocker blocker(m_Spins[i][c]);
			m_Spins[i][c]->setValue(static_cast<double>(m_Values[i][static_cast<int>(c)]));
		}
		RefreshSwatch(i);
	}
}

void
SurfaceOutputNode::SetValue(size_t index, const glm::vec4& value)
{
	glm::vec4 next(0.0f);
	for (uint32_t c = 0; c < bgl::SurfaceValueComponents(m_Surface.params.values[index].type); ++c)
		next[static_cast<int>(c)] = value[static_cast<int>(c)];

	if (m_Values[index] == next)
		return;

	m_Values[index] = next;
	SyncWidgets();
	Q_EMIT Changed();
}

void
SurfaceOutputNode::PickColor(size_t index)
{
	const bgl::SurfaceValue& value    = m_Surface.params.values[index];
	const bool               hasAlpha = value.type == bgl::SurfaceValueType::kFloat4;
	const glm::vec4          current  = m_Values[index];

	const QColor picked = QColorDialog::getColor(
		QColor::fromRgbF(
			std::clamp(current.r, 0.0f, 1.0f),
			std::clamp(current.g, 0.0f, 1.0f),
			std::clamp(current.b, 0.0f, 1.0f),
			hasAlpha ? std::clamp(current.a, 0.0f, 1.0f) : 1.0f),
		DialogOwnerFor(m_Widget),
		QString::fromStdString(value.name),
		hasAlpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());

	if (!picked.isValid())
		return;

	SetValue(
		index,
		glm::vec4(
			static_cast<float>(picked.redF()),
			static_cast<float>(picked.greenF()),
			static_cast<float>(picked.blueF()),
			static_cast<float>(picked.alphaF())));
}

void
SurfaceOutputNode::RefreshSwatch(size_t index)
{
	QPushButton* swatch = index < m_Swatches.size() ? m_Swatches[index] : nullptr;
	if (swatch == nullptr)
		return;

	const glm::vec4 value = m_Values[index];
	const QColor    color = QColor::fromRgbF(
		std::clamp(value.r, 0.0f, 1.0f),
		std::clamp(value.g, 0.0f, 1.0f),
		std::clamp(value.b, 0.0f, 1.0f));

	// The swatch is opaque; a float4's alpha is shown as text so a fully transparent colour is
	// still readable, as the PBR sink shows its base colour factor.
	swatch->setStyleSheet(
		QStringLiteral("background-color: %1; border: 1px solid #202020;").arg(color.name()));
	if (m_Surface.params.values[index].type == bgl::SurfaceValueType::kFloat4)
		swatch->setText(QStringLiteral("A %1").arg(value.a, 0, 'f', 2));
}

QJsonObject
SurfaceOutputNode::save() const
{
	QJsonObject json = NodeDelegateModel::save();

	auto parameters = QJsonObject();
	for (size_t i = 0; i < m_Surface.params.values.size(); ++i)
	{
		const bgl::SurfaceValue& value = m_Surface.params.values[i];

		auto components = QJsonArray();
		for (uint32_t c = 0; c < bgl::SurfaceValueComponents(value.type); ++c)
			components.append(static_cast<double>(m_Values[i][static_cast<int>(c)]));

		parameters[QString::fromStdString(value.name)] = components;
	}
	json["parameters"] = parameters;

	json["alphaMode"]   = QLatin1String(c_AlphaModeNames[static_cast<size_t>(m_AlphaMode)]);
	json["alphaCutoff"] = static_cast<double>(m_AlphaCutoff);
	json["doubleSided"] = m_DoubleSided;

	return json;
}

void
SurfaceOutputNode::load(const QJsonObject& json)
{
	const QJsonObject parameters = json["parameters"].toObject();
	for (size_t i = 0; i < m_Surface.params.values.size(); ++i)
	{
		const bgl::SurfaceValue& value = m_Surface.params.values[i];

		const QJsonValue stored = parameters[QString::fromStdString(value.name)];
		if (stored.isUndefined())
			continue;

		// A single number and a one-element list mean the same thing, as they do in the document.
		const QJsonArray components =
			stored.isArray() ? stored.toArray() : QJsonArray{ stored.toDouble() };

		glm::vec4  loaded(0.0f);
		const auto count = std::min(
			static_cast<uint32_t>(components.size()),
			bgl::SurfaceValueComponents(value.type));
		for (uint32_t c = 0; c < count; ++c)
			loaded[static_cast<int>(c)] =
				static_cast<float>(components[static_cast<int>(c)].toDouble());

		m_Values[i] = loaded;
	}

	// A key naming no declared value is dropped, like a texture binding naming no slot -- both
	// are refused at CreateSurfaceMaterial, and the board cannot show them.
	for (auto it = parameters.begin(); it != parameters.end(); ++it)
	{
		const auto declared =
			std::ranges::any_of(m_Surface.params.values, [&](const bgl::SurfaceValue& value) {
				return it.key() == value.name.c_str();
			});
		if (!declared)
		{
			qWarning(
				"MaterialEditor: value '%s' is not declared by surface '%s'",
				qPrintable(it.key()),
				m_Surface.name.c_str());
		}
	}

	const QString mode = json["alphaMode"].toString();
	for (size_t i = 0; i < std::size(c_AlphaModeNames); ++i)
	{
		if (mode == QLatin1String(c_AlphaModeNames[i]))
			m_AlphaMode = static_cast<assetlib::AlphaMode>(i);
	}

	if (json.contains("alphaCutoff"))
		m_AlphaCutoff = static_cast<float>(json["alphaCutoff"].toDouble());
	if (json.contains("doubleSided"))
		m_DoubleSided = json["doubleSided"].toBool();

	SyncWidgets();
	Q_EMIT Changed();
}

void
SurfaceOutputNode::CompileInto(assetlib::BMaterial& material, const std::filesystem::path& dataRoot)
	const
{
	material.shadingModel = assetlib::ShadingModel::kPbrSurface;

	material.layer.alphaMode   = m_AlphaMode;
	material.layer.alphaCutoff = m_AlphaCutoff;
	material.layer.doubleSided = m_DoubleSided;

	assetlib::SurfaceParams& surface = material.surface;
	surface.name                     = m_Surface.name;

	surface.values.clear();
	surface.values.reserve(m_Surface.params.values.size());
	for (size_t i = 0; i < m_Surface.params.values.size(); ++i)
	{
		const bgl::SurfaceValue& value = m_Surface.params.values[i];

		auto binding = assetlib::SurfaceValueBinding();
		binding.name = value.name;
		for (uint32_t c = 0; c < bgl::SurfaceValueComponents(value.type); ++c)
			binding.value.push_back(m_Values[i][static_cast<int>(c)]);

		surface.values.push_back(std::move(binding));
	}

	surface.textures.clear();
	for (size_t slot = 0; slot < m_Bound.size(); ++slot)
	{
		const bool routed = SlotIsRouted(slot);
		const bool bound  = m_Bound[slot] != nullptr && !m_Bound[slot]->Path().isEmpty();
		if (!routed && !bound)
			continue;

		auto binding = assetlib::SurfaceTextureBinding();
		binding.name = m_Surface.params.textures[slot].name;

		if (routed)
		{
			// The wires are the routes, exactly as on the PBR board; the bake's stamps and the
			// baked map are the document's own state, which material_io preserves across a save.
			for (uint32_t c = 0; c < assetlib::c_SurfaceSlotChannelCount; ++c)
			{
				const ChannelData::Route route = RouteFor(slot, c);
				if (route.path.isEmpty())
					continue;

				binding.routes[c].texture = Rebase(route.path, dataRoot, true).toStdString();
				binding.routes[c].channel = route.channel;
			}
		}
		else
			binding.texturePath = Rebase(m_Bound[slot]->Path(), dataRoot, true).toStdString();

		surface.textures.push_back(std::move(binding));
	}
}

QJsonObject
SurfaceOutputNode::DocumentState(const assetlib::BMaterial& material)
{
	auto parameters = QJsonObject();
	for (const assetlib::SurfaceValueBinding& value : material.surface.values)
	{
		auto components = QJsonArray();
		for (const float component : value.value) components.append(static_cast<double>(component));
		parameters[QString::fromStdString(value.name)] = components;
	}

	auto state          = QJsonObject();
	state["parameters"] = parameters;

	state["alphaMode"] =
		QLatin1String(c_AlphaModeNames[static_cast<size_t>(material.layer.alphaMode)]);
	state["alphaCutoff"] = static_cast<double>(material.layer.alphaCutoff);
	state["doubleSided"] = material.layer.doubleSided;

	return state;
}

glm::vec4
SurfaceOutputNode::Value(size_t index) const
{
	return index < m_Values.size() ? m_Values[index] : glm::vec4(0.0f);
}

QString
SurfaceOutputNode::BoundTexture(size_t slot) const
{
	return slot < m_Bound.size() && m_Bound[slot] != nullptr ? m_Bound[slot]->Path() : QString();
}

bgl::TextureAssetHandle
SurfaceOutputNode::BoundTextureAsset(size_t slot) const
{
	return slot < m_Bound.size() && m_Bound[slot] != nullptr ? m_Bound[slot]->Texture() :
	                                                           bgl::TextureAssetHandle();
}
