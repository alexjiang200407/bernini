#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsProxyWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalBlocker>

namespace
{
	// Caption of each group's single collapsed port.
	const char* const c_GroupCaptions[MaterialOutputNode::c_GroupCount] = { "Base Color",
		                                                                    "ORM",
		                                                                    "Normal" };

	QColor
	ToColor(const glm::vec4& factor)
	{
		return QColor::fromRgbF(factor.r, factor.g, factor.b, factor.a);
	}

	glm::vec4
	ToFactor(const QColor& color)
	{
		return glm::vec4(
			static_cast<float>(color.redF()),
			static_cast<float>(color.greenF()),
			static_cast<float>(color.blueF()),
			static_cast<float>(color.alphaF()));
	}

	QDoubleSpinBox*
	MakeFactorSpin(QWidget* parent, double value)
	{
		auto* spin = new QDoubleSpinBox(parent);
		spin->setRange(0.0, 1.0);
		spin->setSingleStep(0.05);
		spin->setDecimals(3);
		spin->setValue(value);
		return spin;
	}
}

MaterialOutputNode::MaterialOutputNode() : MaterialOutputNode(3) {}

MaterialOutputNode::MaterialOutputNode(unsigned int baseColorArity)
{
	m_GroupSizes[0] = std::min(baseColorArity, c_GroupChannels[0]);
}

void
MaterialOutputNode::AddExtraRows(QWidget*, QFormLayout*)
{}

unsigned int
MaterialOutputNode::GroupChannelOffset(unsigned int group)
{
	unsigned int offset = 0;
	for (unsigned int g = 0; g < group && g < c_GroupCount; ++g) offset += c_GroupChannels[g];
	return offset;
}

bool
MaterialOutputNode::IsCollapsed(unsigned int group) const
{
	return group < c_GroupCount && m_GroupPorts[group] == 1 && m_GroupSizes[group] > 1;
}

unsigned int
MaterialOutputNode::GroupFirstPort(unsigned int group) const
{
	unsigned int first = 0;
	for (unsigned int g = 0; g < group && g < c_GroupCount; ++g) first += m_GroupPorts[g];
	return first;
}

MaterialOutputNode::PortRef
MaterialOutputNode::ResolvePort(QtNodes::PortIndex port) const
{
	if (port < 0)
		return {};

	auto remaining = static_cast<unsigned int>(port);
	for (unsigned int g = 0; g < c_GroupCount; ++g)
	{
		if (remaining < m_GroupPorts[g])
			return { g, remaining };
		remaining -= m_GroupPorts[g];
	}
	return {};  // group == c_GroupCount: out of range
}

unsigned int
MaterialOutputNode::nPorts(QtNodes::PortType portType) const
{
	if (portType != QtNodes::PortType::In)
		return 0u;

	unsigned int total = 0;
	for (const unsigned int count : m_GroupPorts) total += count;
	return total;
}

QtNodes::NodeDataType
MaterialOutputNode::dataType(QtNodes::PortType, QtNodes::PortIndex port) const
{
	const PortRef ref = ResolvePort(port);
	if (ref.group >= c_GroupCount)
		return ChannelData::Type(1);

	// One wide port takes the whole group; expanded, each port takes a single channel. The opaque
	// node's base color is a 3-wide RGB port, so a texture's RGBA bundle will not connect to it.
	return ChannelData::Type(IsCollapsed(ref.group) ? m_GroupSizes[ref.group] : 1);
}

void
MaterialOutputNode::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex port)
{
	const PortRef ref = ResolvePort(port);
	if (ref.group >= c_GroupCount)
		return;

	// QtNodes pushes a null payload when a wire is removed, so this covers connect and disconnect.
	auto channelData = std::dynamic_pointer_cast<ChannelData>(data);

	if (IsCollapsed(ref.group))
		m_Bundles[ref.group] = std::move(channelData);
	else
		m_Channels[GroupChannelOffset(ref.group) + ref.offset] = std::move(channelData);

	Q_EMIT Changed();
}

ChannelData::Route
MaterialOutputNode::Route(unsigned int index) const
{
	if (index >= c_ChannelCount)
		return {};

	// Locate the group this output channel belongs to, and its position inside that group.
	unsigned int group  = 0;
	unsigned int offset = index;
	while (group < c_GroupCount && offset >= c_GroupChannels[group])
	{
		offset -= c_GroupChannels[group];
		++group;
	}
	if (group >= c_GroupCount)
		return {};

	if (offset >= m_GroupSizes[group])
		return {};

	// A collapsed group draws every channel from its one wide payload; an expanded one from the
	// per-channel payload wired into that channel's own port.
	if (IsCollapsed(group))
		return m_Bundles[group] ? m_Bundles[group]->At(offset) : ChannelData::Route{};

	const std::shared_ptr<ChannelData>& channel = m_Channels[index];
	return channel ? channel->At(0) : ChannelData::Route{};
}

void
MaterialOutputNode::SetGroupExpanded(unsigned int group, bool expanded)
{
	if (group >= c_GroupCount)
		return;

	const unsigned int oldCount = m_GroupPorts[group];
	const unsigned int newCount = expanded ? m_GroupSizes[group] : 1u;
	if (oldCount == newCount)
		return;

	const auto first = static_cast<QtNodes::PortIndex>(GroupFirstPort(group));

	// Drop the group's ports, then add its new ones. QtNodes reads nPorts() inside each "about to"
	// call to rebase the surviving connections, so the count must change *between* the paired
	// signals: still old when the removal is announced, already new when the insertion is.
	Q_EMIT portsAboutToBeDeleted(
		QtNodes::PortType::In,
		first,
		first + static_cast<QtNodes::PortIndex>(oldCount) - 1);
	m_GroupPorts[group] = 0;
	Q_EMIT portsDeleted();

	Q_EMIT portsAboutToBeInserted(
		QtNodes::PortType::In,
		first,
		first + static_cast<QtNodes::PortIndex>(newCount) - 1);
	m_GroupPorts[group] = newCount;
	Q_EMIT portsInserted();

	// The removed ports took their wires with them, so the group is now unrouted either way.
	m_Bundles[group]                 = nullptr;
	const unsigned int channelOffset = GroupChannelOffset(group);
	for (unsigned int i = 0; i < c_GroupChannels[group]; ++i)
		m_Channels[channelOffset + i] = nullptr;

	// The port signals rebase connections but do not repaint; this is what redraws the node.
	Q_EMIT requestNodeUpdate();
	Q_EMIT Changed();
}

QWidget*
MaterialOutputNode::embeddedWidget()
{
	if (m_Widget != nullptr)
		return m_Widget;

	m_Widget   = new QWidget();
	auto* form = new QFormLayout(m_Widget);
	form->setContentsMargins(4, 4, 4, 4);

	m_ColorButton = new QPushButton(m_Widget);
	m_ColorButton->setFlat(true);
	m_ColorButton->setAutoFillBackground(true);
	RefreshColorSwatch();
	form->addRow(QStringLiteral("Base Color"), m_ColorButton);

	m_Metallic = MakeFactorSpin(m_Widget, m_MetallicFactor);
	form->addRow(QStringLiteral("Metallic"), m_Metallic);

	m_Roughness = MakeFactorSpin(m_Widget, m_RoughnessFactor);
	form->addRow(QStringLiteral("Roughness"), m_Roughness);

	AddExtraRows(m_Widget, form);

	// Queued, not called directly: the click arrives while the proxy widget is dispatching the mouse
	// event, and QColorDialog::getColor spins a nested event loop. Opening it once the proxy has
	// finished with the event leaves the graphics scene in a consistent state.
	connect(
		m_ColorButton,
		&QPushButton::clicked,
		this,
		&MaterialOutputNode::PickBaseColor,
		Qt::QueuedConnection);

	connect(m_Metallic, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_MetallicFactor = static_cast<float>(value);
		Q_EMIT Changed();
	});

	connect(m_Roughness, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_RoughnessFactor = static_cast<float>(value);
		Q_EMIT Changed();
	});

	// Normal has only X and Y, but it still expands -- a normal map's R and G can come from
	// different sources, and Z is reconstructed in the shader either way.
	for (unsigned int group = 0; group < c_GroupCount; ++group)
	{
		auto* box = new QCheckBox(m_Widget);
		box->setChecked(m_GroupPorts[group] > 1);
		box->setToolTip(QStringLiteral("Route this group's channels individually"));
		form->addRow(QStringLiteral("Split %1").arg(QLatin1String(c_GroupCaptions[group])), box);

		connect(box, &QCheckBox::toggled, this, [this, group](bool checked) {
			SetGroupExpanded(group, checked);
		});

		m_ExpandBoxes[group] = box;
	}

	return m_Widget;
}

void
MaterialOutputNode::PickBaseColor()
{
	// The dialog must NOT be parented to m_Widget. An embedded widget is reparented into a
	// QGraphicsProxyWidget, and Qt embeds a proxied widget's child windows into the graphics scene
	// too -- so the dialog's real window comes up blank while its contents are painted onto the node
	// canvas, and the scene is left with a stray proxy afterwards. Parent it to the editor's actual
	// top-level window instead; a null parent would also work but would lose modality and taskbar
	// grouping.
	QWidget* owner = m_Widget != nullptr ? m_Widget->window() : nullptr;
	if (owner == nullptr || owner->graphicsProxyWidget() != nullptr)
		owner = QApplication::activeWindow();

	const QColor picked = QColorDialog::getColor(
		ToColor(m_BaseColorFactor),
		owner,
		QStringLiteral("Base Color Factor"),
		QColorDialog::ShowAlphaChannel);

	if (!picked.isValid())
		return;

	m_BaseColorFactor = ToFactor(picked);
	RefreshColorSwatch();
	Q_EMIT Changed();
}

void
MaterialOutputNode::RefreshColorSwatch()
{
	if (m_ColorButton == nullptr)
		return;

	const QColor color = ToColor(m_BaseColorFactor);

	// The swatch is opaque; alpha is shown as text so a fully transparent factor is still readable.
	m_ColorButton->setStyleSheet(
		QStringLiteral("background-color: %1; border: 1px solid #202020;").arg(color.name()));
	m_ColorButton->setText(QStringLiteral("A %1").arg(m_BaseColorFactor.a, 0, 'f', 2));
}

QJsonObject
MaterialOutputNode::save() const
{
	QJsonObject json = NodeDelegateModel::save();

	json["baseColorR"] = m_BaseColorFactor.r;
	json["baseColorG"] = m_BaseColorFactor.g;
	json["baseColorB"] = m_BaseColorFactor.b;
	json["baseColorA"] = m_BaseColorFactor.a;
	json["metallic"]   = m_MetallicFactor;
	json["roughness"]  = m_RoughnessFactor;

	// Which groups are split, so a reloaded graph has the same ports its connections refer to.
	auto split = QJsonArray();
	for (const unsigned int count : m_GroupPorts) split.append(count > 1);
	json["split"] = split;

	return json;
}

void
MaterialOutputNode::load(const QJsonObject& json)
{
	const auto factor = [&json](const char* key, float fallback) {
		const QJsonValue value = json[QLatin1String(key)];
		return value.isDouble() ? static_cast<float>(value.toDouble()) : fallback;
	};

	m_BaseColorFactor = glm::vec4(
		factor("baseColorR", 1.0f),
		factor("baseColorG", 1.0f),
		factor("baseColorB", 1.0f),
		factor("baseColorA", 1.0f));
	m_MetallicFactor  = factor("metallic", 1.0f);
	m_RoughnessFactor = factor("roughness", 1.0f);

	// Restore the port layout by assigning the counts directly: load() runs while the node is being
	// created, before any connection has been restored, so there is nothing to rebase and the
	// insert/remove signals SetGroupExpanded emits would be premature.
	const QJsonArray split = json["split"].toArray();
	for (unsigned int group = 0; group < c_GroupCount; ++group)
	{
		const bool expanded = group < static_cast<unsigned int>(split.size()) &&
		                      split[static_cast<int>(group)].toBool();
		m_GroupPorts[group] = expanded ? m_GroupSizes[group] : 1u;
	}

	// The widgets only exist once the node has been shown; keep them in step when they do.
	RefreshColorSwatch();
	if (m_Metallic != nullptr)
		m_Metallic->setValue(m_MetallicFactor);
	if (m_Roughness != nullptr)
		m_Roughness->setValue(m_RoughnessFactor);
	for (unsigned int group = 0; group < c_GroupCount; ++group)
	{
		if (m_ExpandBoxes[group] != nullptr)
		{
			const QSignalBlocker blocker(m_ExpandBoxes[group]);
			m_ExpandBoxes[group]->setChecked(m_GroupPorts[group] > 1);
		}
	}

	Q_EMIT Changed();
}

QString
MaterialOutputNode::portCaption(QtNodes::PortType, QtNodes::PortIndex port) const
{
	// Order matches bgl::idl::PbrChannel.
	static const char* const c_Captions[c_ChannelCount] = { "Base R",   "Base G",   "Base B",
		                                                    "Base A",   "AO",       "Roughness",
		                                                    "Metallic", "Normal X", "Normal Y" };

	const PortRef ref = ResolvePort(port);
	if (ref.group >= c_GroupCount)
		return {};

	if (IsCollapsed(ref.group))
		return QString::fromLatin1(c_GroupCaptions[ref.group]);

	return QString::fromLatin1(c_Captions[GroupChannelOffset(ref.group) + ref.offset]);
}
