#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/nodes/SurfaceTextureData.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSignalBlocker>
#include <algorithm>
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <glm/vec4.hpp>
#include <iterator>
#include <memory>
#include <qlatin1stringview.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <string>
#include <utility>

namespace
{
	// The saved graph's words for AlphaMode, indexed by the enum -- also the layer combo's order.
	// The graph is the editor's own blob; matching the document's words is convenience, not
	// contract.
	constexpr const char* c_AlphaModeNames[]  = { "opaque", "mask", "blend", "hashed" };
	constexpr const char* c_AlphaModeLabels[] = { "Opaque",
		                                          "Alpha Tested",
		                                          "Alpha Blend",
		                                          "Hashed Alpha" };

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
	return portType == QtNodes::PortType::In ?
	           static_cast<unsigned int>(m_Surface.params.textures.size()) :
	           0u;
}

QtNodes::NodeDataType
SurfaceOutputNode::dataType(QtNodes::PortType, QtNodes::PortIndex) const
{
	return SurfaceTextureData::Type();
}

void
SurfaceOutputNode::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex port)
{
	const auto slot = static_cast<size_t>(port);
	if (slot >= m_Bound.size())
		return;

	m_Bound[slot] = std::dynamic_pointer_cast<SurfaceTextureData>(data);
	Q_EMIT Changed();
}

QString
SurfaceOutputNode::portCaption(QtNodes::PortType, QtNodes::PortIndex port) const
{
	const auto slot = static_cast<size_t>(port);
	if (slot >= m_Surface.params.textures.size())
		return {};

	const bgl::SurfaceTexture& texture = m_Surface.params.textures[slot];
	return QStringLiteral("%1 (%2)").arg(
		QString::fromStdString(texture.name),
		QLatin1String(KindWord(texture.kind)));
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
				Q_EMIT Changed();
			});
		}

		form->addRow(QString::fromStdString(value.name), field);
	}

	// The layer keys are every model's, and with the Output selector holding the surface they are
	// authored here.
	m_LayerBox = new QComboBox(m_Widget);
	for (const char* label : c_AlphaModeLabels) m_LayerBox->addItem(QLatin1String(label));
	m_LayerBox->setCurrentIndex(static_cast<int>(m_AlphaMode));
	form->addRow(QStringLiteral("Layer"), m_LayerBox);

	connect(m_LayerBox, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index < 0 || index > static_cast<int>(assetlib::AlphaMode::kHashed))
			return;
		m_AlphaMode = static_cast<assetlib::AlphaMode>(index);
		SyncWidgets();
		Q_EMIT Changed();
	});

	m_CutoffSpin = new QDoubleSpinBox(m_Widget);
	m_CutoffSpin->setRange(0.0, 1.0);
	m_CutoffSpin->setSingleStep(0.05);
	m_CutoffSpin->setDecimals(3);
	m_CutoffSpin->setValue(static_cast<double>(m_AlphaCutoff));
	form->addRow(QStringLiteral("Alpha Cutoff"), m_CutoffSpin);

	connect(m_CutoffSpin, &QDoubleSpinBox::valueChanged, this, [this](double edited) {
		m_AlphaCutoff = static_cast<float>(edited);
		Q_EMIT Changed();
	});

	m_DoubleSidedBox = new QCheckBox(m_Widget);
	m_DoubleSidedBox->setChecked(m_DoubleSided);
	form->addRow(QStringLiteral("Double Sided"), m_DoubleSidedBox);

	connect(m_DoubleSidedBox, &QCheckBox::toggled, this, [this](bool checked) {
		m_DoubleSided = checked;
		Q_EMIT Changed();
	});

	SyncWidgets();
	return m_Widget;
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
	}

	{
		const QSignalBlocker blocker(m_LayerBox);
		m_LayerBox->setCurrentIndex(static_cast<int>(m_AlphaMode));
	}
	{
		const QSignalBlocker blocker(m_CutoffSpin);
		m_CutoffSpin->setValue(static_cast<double>(m_AlphaCutoff));
	}
	{
		const QSignalBlocker blocker(m_DoubleSidedBox);
		m_DoubleSidedBox->setChecked(m_DoubleSided);
	}

	// The cutoff is read on a mask layer alone -- hashed replaces it with stochastic coverage.
	auto* form = static_cast<QFormLayout*>(m_Widget->layout());
	form->setRowVisible(m_CutoffSpin, m_AlphaMode == assetlib::AlphaMode::kMask);
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
		if (m_Bound[slot] == nullptr || m_Bound[slot]->Path().isEmpty())
			continue;

		auto binding    = assetlib::SurfaceTextureBinding();
		binding.name    = m_Surface.params.textures[slot].name;
		binding.texture = Rebase(m_Bound[slot]->Path(), dataRoot, true).toStdString();
		surface.textures.push_back(std::move(binding));
	}
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
