#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <bgl/types/TextureAssetHandle.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <filesystem>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include "Windows/MeshEditor/nodes/ChannelData.h"
#include "Windows/MeshEditor/nodes/MaterialSinkNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <array>

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class SurfaceTextureData;

/**
 * The sink for a material drawn by a game-defined surface, generated from reflection: one input
 * port per texture slot, one spin-box row per value with the declaration's default prefilled.
 * The layer keys are its state too, edited from the properties panel (ADR-9).
 *
 * One is registered per surface the engine reflected, named `SurfaceOutput:<surface>`. A slot's
 * whole-texture port carries SurfaceTextureData. A *data* slot can be split, as a PBR sink's group
 * is: its whole port gives way to one channel port per component, carrying single-channel
 * ChannelData -- whole or channels, never both (ADR-7). Colour, normal and coverage slots stay
 * whole-bound.
 */
class SurfaceOutputNode : public MaterialSinkNode
{
	Q_OBJECT

public:
	// `language` must outlive the node.
	SurfaceOutputNode(const editor::ILanguageResolver& language, bgl::SurfaceType surface);

	QString
	caption() const override;

	QString
	name() const override;

	/** The registered model name for a surface: `SurfaceOutput:<surface>`. */
	[[nodiscard]] static QString
	ModelNameFor(const std::string& surfaceName);

	unsigned int
	nPorts(QtNodes::PortType portType) const override;

	QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override;

	std::shared_ptr<QtNodes::NodeData>
	outData(QtNodes::PortIndex) override
	{
		return nullptr;
	}

	void
	setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex port) override;

	QWidget*
	embeddedWidget() override;

	QString
	portCaption(QtNodes::PortType, QtNodes::PortIndex port) const override;

	bool
	portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override
	{
		return true;
	}

	QJsonObject
	save() const override;
	void
	load(const QJsonObject& json) override;

	void
	CompileInto(assetlib::BMaterial& material, const std::filesystem::path& dataRoot)
		const override;

	/**
	 * The document's surface bindings and layer keys as this node's own load() state -- what seeds
	 * a board for a material that has no authored surface board. Values the document does not set
	 * are absent, so load() leaves them at the declaration's defaults.
	 */
	[[nodiscard]] static QJsonObject
	DocumentState(const assetlib::BMaterial& material);

	[[nodiscard]] const bgl::SurfaceType&
	Surface() const noexcept
	{
		return m_Surface;
	}

	/** The current value of `params.values[index]`; components past the type's stay zero. */
	[[nodiscard]] glm::vec4
	Value(size_t index) const;

	/**
	 * Writes `params.values[index]` -- what the colour picker lands. Components past the type's
	 * are zeroed, the row's widgets follow, and Changed is emitted on an actual change.
	 */
	void
	SetValue(size_t index, const glm::vec4& value);

	/** How a port index maps onto the declared slots: a slot has one whole-texture port, or -- a
	 *  data slot that is split -- one channel port per component in its place. */
	struct PortRef
	{
		size_t   slot      = SIZE_MAX;  // == textures.size() when the index is out of range
		bool     whole     = true;
		uint32_t component = 0;
	};

	[[nodiscard]] PortRef
	ResolvePort(QtNodes::PortIndex port) const;

	/**
	 * The whole-texture port of `slot`: its first port. Splitting a slot shifts every later slot's
	 * ports, so a caller that wires the board must ask rather than hold literal indices.
	 */
	[[nodiscard]] unsigned int
	WholePortFor(size_t slot) const;

	/**
	 * The port `slot`'s `component` connects to, honouring whether the slot is split: its own
	 * channel port when it is, the one whole port when it is not -- as MaterialOutputNode::GroupPort
	 * answers a collapsed group's wide port.
	 */
	[[nodiscard]] unsigned int
	ChannelPortFor(size_t slot, uint32_t component) const;

	/** Whether data slot `slot` shows its channel ports in place of its whole port. */
	[[nodiscard]] bool
	IsSplit(size_t slot) const;

	/**
	 * Splits data slot `slot` into channel ports, or joins it back into its whole port. The slot's
	 * old ports go and take their wires with them, as unsplitting a PBR group does; a slot that is
	 * not a data slot does not split.
	 */
	void
	SetSplit(size_t slot, bool split);

	/** Whether anything is wired into `slot`'s channel ports. */
	[[nodiscard]] bool
	SlotIsRouted(size_t slot) const;

	/**
	 * Whether `port` may take a wire: a slot is bound whole or composited from routes, never both
	 * (ADR-7), so each kind refuses while the other is wired. The model asks on every offered
	 * connection.
	 */
	[[nodiscard]] bool
	PortAccepts(QtNodes::PortIndex port) const;

	/** The route wired into `slot`'s `component` channel port; empty path when unwired. */
	[[nodiscard]] ChannelData::Route
	RouteFor(size_t slot, uint32_t component) const;

	/** The path bound into texture slot `slot`, empty while nothing is wired there. */
	[[nodiscard]] QString
	BoundTexture(size_t slot) const;

	/** The uploaded texture bound into slot `slot`; null while nothing is wired, and null when
	 *  there was no device to upload through -- the surface then samples its default. */
	[[nodiscard]] bgl::TextureAssetHandle
	BoundTextureAsset(size_t slot) const;

	[[nodiscard]] assetlib::AlphaMode
	GetAlphaMode() const noexcept
	{
		return m_AlphaMode;
	}

	[[nodiscard]] float
	GetAlphaCutoff() const noexcept
	{
		return m_AlphaCutoff;
	}

	[[nodiscard]] bool
	GetDoubleSided() const noexcept
	{
		return m_DoubleSided;
	}

	// The layer keys are authored in the properties panel (ADR-9); these are what its widgets
	// write. Each emits Changed, so the preview follows a panel edit as it follows a board edit.
	void
	SetAlphaMode(assetlib::AlphaMode mode);

	void
	SetAlphaCutoff(float cutoff);

	void
	SetDoubleSided(bool doubleSided);

private:
	void
	SyncWidgets();

	// The ports `slot` takes: its channel count when split, else 1.
	[[nodiscard]] unsigned int
	SlotPortCount(size_t slot) const;

	// The ports `split` gives `slot`.
	[[nodiscard]] static unsigned int
	PortsWhen(bool split);

	void
	PickColor(size_t index);

	void
	RefreshSwatch(size_t index);

	const editor::ILanguageResolver& m_Language;
	bgl::SurfaceType                 m_Surface;

	// One vec4 per declared value, seeded from the defaults; authoritative over the spin boxes,
	// which round to their decimals.
	std::vector<glm::vec4> m_Values;

	// One entry per texture slot; null while nothing is wired.
	std::vector<std::shared_ptr<SurfaceTextureData>> m_Bound;

	// One entry per texture slot and component; only a data slot's are reachable, null while
	// nothing is wired there.
	std::vector<std::array<std::shared_ptr<ChannelData>, assetlib::c_SurfaceSlotChannelCount>>
		m_Routes;

	// The ports each texture slot takes: 1, or a split data slot's channel count -- and 0 for the
	// moment SetSplit swaps them. Saved as which slots are split, by name, so a surface that
	// reorders its slots still loads the split it had.
	std::vector<unsigned int> m_SlotPorts;

	assetlib::AlphaMode m_AlphaMode   = assetlib::AlphaMode::kOpaque;
	float               m_AlphaCutoff = 0.5f;
	bool                m_DoubleSided = true;

	QWidget*                                  m_Widget = nullptr;
	std::vector<std::vector<QDoubleSpinBox*>> m_Spins;

	// Parallel to m_Values; null everywhere but a `[Color]` value's row.
	std::vector<QPushButton*> m_Swatches;

	// Parallel to the texture slots; null everywhere but a data slot's row.
	std::vector<QCheckBox*> m_SplitBoxes;
};
