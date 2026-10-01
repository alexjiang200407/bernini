#pragma once

#include "Windows/MeshEditor/lod_view.h"

#include <QComboBox>
#include <QString>
#include <qtmetamacros.h>

#include <cstdint>
#include <optional>

class QWidget;

namespace editor
{
	class ILanguageResolver;

	/**
	 * The Level of Detail selector the Mesh, Animation and Blend Space editors share. Entry 0 is
	 * Auto, which names the level the preview draws; the rest pin one. It knows no preview: a window
	 * feeds it the shown mesh's levels and the readouts, and acts on the pin it reports.
	 */
	class LodSelector final : public QComboBox
	{
		Q_OBJECT

	public:
		explicit LodSelector(const ILanguageResolver& language, QWidget* parent = nullptr);

		/** The caption a window puts above it. */
		[[nodiscard]] static QString
		Label(const ILanguageResolver& language);

		/**
		 * Lists `lods`' levels after Auto -- none: Auto alone, disabled -- with `forced` selected
		 * where the list reaches it, and Auto otherwise. Reports nothing.
		 */
		void
		Refresh(const MeshLods* lods, std::optional<uint32_t> forced);

		/**
		 * Names the level `readout` draws in the Auto entry (`Auto: LOD 1`, or `Auto: nothing
		 * drawn` past the last level); plain Auto while a level is pinned or without a readout.
		 */
		void
		ShowAuto(std::optional<LodReadout> readout);

		/** The level pinned, or none for Auto. */
		[[nodiscard]] std::optional<uint32_t>
		GetForcedLevel() const;

	Q_SIGNALS:
		/** A pick: the level to pin, or none for Auto. Refresh never emits it. */
		void
		ForcedLevelChanged(std::optional<uint32_t> level);

	private:
		const ILanguageResolver& m_Language;
		uint32_t                 m_LevelCount = 0;
	};
}
