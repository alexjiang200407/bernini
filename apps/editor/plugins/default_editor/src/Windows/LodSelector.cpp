#include "Windows/LodSelector.h"
#include "Windows/MeshEditor/lod_view.h"

#include <QComboBox>
#include <QSignalBlocker>
#include <QVariant>
#include <QWidget>
#include <qtmetamacros.h>

#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/localize.h>

#include <cstdint>
#include <optional>

namespace editor
{
	LodSelector::LodSelector(const ILanguageResolver& language, QWidget* parent) :
		QComboBox(parent), m_Language(language)
	{
		setToolTip(Localize(
			m_Language,
			"bernini.lod.selector_tooltip",
			"Auto draws the level the game would at this size on screen. A level pins it, "
			"however near or far the camera is."));
		Refresh(nullptr, std::nullopt);

		connect(this, &QComboBox::currentIndexChanged, this, [this]() {
			Q_EMIT ForcedLevelChanged(GetForcedLevel());
		});
	}

	QString
	LodSelector::Label(const ILanguageResolver& language)
	{
		return Localize(language, "bernini.lod.label", "Level of Detail");
	}

	void
	LodSelector::Refresh(const MeshLods* lods, const std::optional<uint32_t> forced)
	{
		const QSignalBlocker blocker(this);
		clear();
		addItem(Localize(m_Language, "bernini.lod.auto", "Auto"));

		m_LevelCount = lods != nullptr ? static_cast<uint32_t>(lods->minPixels.size()) : 0u;
		for (uint32_t level = 0; level < m_LevelCount; ++level)
			addItem(Localize(m_Language, "bernini.lod.level", { level }, "LOD {0}"), level);
		m_Impostor = lods != nullptr && lods->impostor;
		if (m_Impostor)
			addItem(Localize(m_Language, "bernini.lod.impostor", "Impostor"), c_ForceImpostor);

		if (forced == c_ForceImpostor && m_Impostor)
			setCurrentIndex(count() - 1);
		else
			setCurrentIndex(
				forced.has_value() && *forced < m_LevelCount ? static_cast<int>(*forced) + 1 : 0);
		setEnabled(lods != nullptr);
	}

	void
	LodSelector::ShowAuto(const std::optional<LodReadout> readout)
	{
		QString text = Localize(m_Language, "bernini.lod.auto", "Auto");
		if (readout.has_value() && !GetForcedLevel().has_value())
		{
			if (readout->level < m_LevelCount)
				text = Localize(
					m_Language,
					"bernini.lod.auto_level",
					{ readout->level },
					"Auto: LOD {0}");
			else if (m_Impostor)
				text = Localize(m_Language, "bernini.lod.auto_impostor", "Auto: Impostor");
			else
				text = Localize(m_Language, "bernini.lod.auto_nothing", "Auto: nothing drawn");
		}
		setItemText(0, text);
	}

	std::optional<uint32_t>
	LodSelector::GetForcedLevel() const
	{
		const QVariant level = currentData();
		return level.isValid() ? std::optional(level.toUInt()) : std::nullopt;
	}
}
