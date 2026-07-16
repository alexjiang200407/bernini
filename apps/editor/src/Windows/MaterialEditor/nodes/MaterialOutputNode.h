#pragma once

#include <QtNodes/NodeDelegateModel>

#include <glm/vec4.hpp>

#include "Windows/MaterialEditor/nodes/ChannelData.h"

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QPushButton;

class MaterialOutputNode : public QtNodes::NodeDelegateModel
{
	Q_OBJECT

public:
	static constexpr unsigned int c_ChannelCount = 9;
	static constexpr unsigned int c_GroupCount   = 3;

	// How many channels each group has in BMaterial::routes. Distinct from how many a node exposes.
	static constexpr std::array<unsigned int, c_GroupCount> c_GroupChannels = { 4, 3, 2 };

	MaterialOutputNode();

	QString
	caption() const override
	{
		return QStringLiteral("Material Output");
	}

	QString
	name() const override
	{
		return QStringLiteral("MaterialOutput");
	}

	unsigned int
	nPorts(QtNodes::PortType portType) const override;

	QtNodes::NodeDataType
	dataType(QtNodes::PortType, QtNodes::PortIndex port) const override;

	std::shared_ptr<QtNodes::NodeData>
	outData(QtNodes::PortIndex) override
	{
		return nullptr;
	}

	void
	setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex port) override;

	QWidget*
	embeddedWidget() override;

	QJsonObject
	save() const override;
	void
	load(const QJsonObject& json) override;

	QString
	portCaption(QtNodes::PortType, QtNodes::PortIndex port) const override;

	bool
	portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override
	{
		return true;
	}

	// The route wired into canonical channel `index`. A channel this node does not expose (the opaque
	// node's base-color alpha) is never routed.
	[[nodiscard]] ChannelData::Route
	Route(unsigned int index) const;

	// Whether the material this node compiles to is a cutout, and the alpha it cuts at.
	[[nodiscard]] virtual bool
	IsAlphaTested() const noexcept
	{
		return false;
	}

	[[nodiscard]] virtual float
	AlphaCutoff() const noexcept
	{
		return 0.5f;
	}

	[[nodiscard]] glm::vec4
	BaseColorFactor() const noexcept
	{
		return m_BaseColorFactor;
	}

	[[nodiscard]] float
	MetallicFactor() const noexcept
	{
		return m_MetallicFactor;
	}

	[[nodiscard]] float
	RoughnessFactor() const noexcept
	{
		return m_RoughnessFactor;
	}

Q_SIGNALS:
	void
	Changed();

protected:
	// `baseColorArity` is 3 (RGB) for an opaque material, 4 (RGBA) for a cutout.
	explicit MaterialOutputNode(unsigned int baseColorArity);

	// Rows appended to the embedded form, after the factors. Nothing by default.
	virtual void
	AddExtraRows(QWidget* parent, QFormLayout* form);

private:
	void
	PickBaseColor();

	void
	RefreshColorSwatch();

	void
	SetGroupExpanded(unsigned int group, bool expanded);

	// A group with more than one channel shows one wide port until it is split.
	[[nodiscard]] bool
	IsCollapsed(unsigned int group) const;

	[[nodiscard]] unsigned int
	GroupFirstPort(unsigned int group) const;

	struct PortRef
	{
		unsigned int group  = c_GroupCount;
		unsigned int offset = 0;
	};
	[[nodiscard]] PortRef
	ResolvePort(QtNodes::PortIndex port) const;

	// First canonical channel of a group: 0, 4, 7.
	[[nodiscard]] static unsigned int
	GroupChannelOffset(unsigned int group);

	// How many channels of each group this node exposes; only base color differs between the two.
	std::array<unsigned int, c_GroupCount> m_GroupSizes = { 3, 3, 2 };
	std::array<unsigned int, c_GroupCount> m_GroupPorts = { 1, 1, 1 };

	std::array<std::shared_ptr<ChannelData>, c_GroupCount>   m_Bundles;
	std::array<std::shared_ptr<ChannelData>, c_ChannelCount> m_Channels;

	glm::vec4 m_BaseColorFactor = glm::vec4(1.0f);
	float     m_MetallicFactor  = 1.0f;
	float     m_RoughnessFactor = 0.2f;

	QWidget*                             m_Widget      = nullptr;
	QPushButton*                         m_ColorButton = nullptr;
	QDoubleSpinBox*                      m_Metallic    = nullptr;
	QDoubleSpinBox*                      m_Roughness   = nullptr;
	std::array<QCheckBox*, c_GroupCount> m_ExpandBoxes = {};
};
