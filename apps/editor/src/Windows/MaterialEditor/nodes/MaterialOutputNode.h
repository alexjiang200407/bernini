#pragma once

#include <QtNodes/NodeDelegateModel>

#include <array>
#include <glm/vec4.hpp>

#include <assetlib_structs/BMaterial.h>
#include <memory>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>

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

	/**
	 * The input port a group's component connects to, honouring whether the group is split.
	 *
	 * Splitting a group shifts every later group's ports, so a caller that wires the board must ask
	 * rather than hold literal indices. A collapsed group answers its one wide port for every
	 * component.
	 *
	 * @param group A group index below c_GroupCount; out of range answers 0.
	 */
	[[nodiscard]] unsigned int
	GroupPort(unsigned int group, unsigned int component) const;

	bool
	portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override
	{
		return true;
	}

	// The route wired into canonical channel `index`. A channel this node does not expose (the opaque
	// node's base-color alpha) is never routed.
	[[nodiscard]] ChannelData::Route
	Route(unsigned int index) const;

	// The alpha mode the material this sink compiles to is authored with; the sink type *is* the
	// choice. Opaque here, overridden by the cutout and blend sinks.
	[[nodiscard]] virtual assetlib::AlphaMode
	GetAlphaMode() const noexcept
	{
		return assetlib::AlphaMode::kOpaque;
	}

	// Whether the material this node compiles to is a cutout, and the alpha it cuts at.
	[[nodiscard]] virtual bool
	IsAlphaTested() const noexcept
	{
		return false;
	}

	[[nodiscard]] virtual float
	GetAlphaCutoff() const noexcept
	{
		return 0.5f;
	}

	// What base-color alpha means on this material: 0 coverage, 1 transmission. Only the blend sink
	// offers the choice, and only a blended material reads it.
	[[nodiscard]] virtual float
	GetTransmission() const noexcept
	{
		return 0.0f;
	}

	// Whether a non-opaque surface draws its back faces; glTF's doubleSided. Every sink but the
	// opaque one offers it, and an opaque material draws front faces only whatever it holds.
	[[nodiscard]] bool
	GetDoubleSided() const noexcept
	{
		return m_DoubleSided;
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

	// glTF's KHR_materials_specular: the colour tints a dielectric's F0, the factor weights the whole
	// specular lobe. Every sink carries them -- specular is not a property of the alpha mode.
	[[nodiscard]] glm::vec3
	GetSpecularColorFactor() const noexcept
	{
		return m_SpecularColorFactor;
	}

	[[nodiscard]] float
	GetSpecularFactor() const noexcept
	{
		return m_SpecularFactor;
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
	// The window a modal dialog must be parented to; never the embedded widget. See the definition.
	[[nodiscard]] QWidget*
	DialogOwner() const;

	void
	PickBaseColor();

	void
	PickSpecularColor();

	void
	RefreshColorSwatch();

	void
	RefreshSpecularSwatch();

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

	glm::vec4 m_BaseColorFactor     = glm::vec4(1.0f);
	float     m_MetallicFactor      = 1.0f;
	float     m_RoughnessFactor     = 0.2f;
	glm::vec3 m_SpecularColorFactor = glm::vec3(1.0f);
	float     m_SpecularFactor      = 1.0f;
	bool      m_DoubleSided         = true;

	QWidget*                             m_Widget              = nullptr;
	QPushButton*                         m_ColorButton         = nullptr;
	QDoubleSpinBox*                      m_Metallic            = nullptr;
	QDoubleSpinBox*                      m_Roughness           = nullptr;
	QPushButton*                         m_SpecularColorButton = nullptr;
	QDoubleSpinBox*                      m_Specular            = nullptr;
	std::array<QCheckBox*, c_GroupCount> m_ExpandBoxes         = {};
	QCheckBox*                           m_DoubleSidedBox      = nullptr;
};
