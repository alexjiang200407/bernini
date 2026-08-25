#pragma once

#include <QString>

#include <assetlib_structs/BMaterial.h>

/**
 * A `.bmaterial` parsed at most once per change to it.
 *
 * The verdicts derived from it are deliberately *not* cached. `assetlib::bakeIsStale` compares the
 * material's route stamps against the textures on disk, so it can change with no change to the
 * `.bmaterial` at all -- and a bake that writes its maps and then fails emits nothing to invalidate
 * on. It is a dozen stats; re-running it is what keeps an externally edited texture visible.
 */
class CachedMaterial
{
public:
	/**
	 * The material at `path`, re-read only when the file has changed since the last call. Read
	 * through a store over `dataRoot`, which is the project the path names a file inside.
	 *
	 * @return Null when `path` is empty or the file cannot be read -- which includes the ordinary
	 *         case of a graph given a path by Save As but not yet written.
	 */
	[[nodiscard]] const assetlib::BMaterial*
	Get(const std::filesystem::path& dataRoot, const QString& path);

	// Drops what was read, so the next Get re-reads. For a caller that has just rewritten the file
	// within the same millisecond the stamp is measured in.
	void
	Forget() noexcept;

private:
	std::optional<assetlib::BMaterial> m_Material;
	QString                            m_Path;
	qint64                             m_Stamp = 0;
};
