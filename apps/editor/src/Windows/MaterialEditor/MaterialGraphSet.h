#pragma once

#include <QString>
#include <QStringList>

#include <bgl/IScene.h>

#include "Windows/MaterialEditor/CachedMaterial.h"

class MaterialGraphModel;
class MaterialGraphScene;

/**
 * The material graphs behind a mesh's submeshes, and the table that maps one to the other.
 *
 * Graphs are keyed by *material* while the submesh selector and every mesh binding are indexed by
 * *submesh* -- because submeshes naming the same material share one graph, so editing it once
 * updates every submesh wearing it. Those are the two halves this holds together, and the only code
 * with any business seeing how they are stored.
 *
 * Every index taken here is checked: an out-of-range submesh has no graph, and Holds says whether a
 * graph index still names one. The selection survives a mesh that shrinks under it for that reason.
 */
class MaterialGraphSet
{
public:
	/** One material's graph, and the submeshes it drives. */
	struct Graph
	{
		std::unique_ptr<MaterialGraphModel> model;
		std::unique_ptr<MaterialGraphScene> scene;

		QString materialPath;

		CachedMaterial onDisk;

		// The live material this graph is previewed through. Created once and rewritten in place on
		// every edit, rather than created anew: a graph compiles on each keystroke, and the scene's
		// loose-material buffer is a fixed-size slot pool.
		bgl::MaterialHandle preview;

		std::vector<uint32_t> submeshes;
	};

	/** Drops every graph and sizes the submesh table for a mesh of `submeshCount` submeshes. */
	void
	Reset(int submeshCount);

	/** Drops every graph and the submesh table with it, for a mesh that is going away entirely. */
	void
	Clear();

	/** A new, empty graph driving `submesh` alone. Returns its index. */
	int
	Add(int submesh);

	/** Points `submesh` at the graph already at `graphIndex`, which then drives both. */
	void
	Share(int graphIndex, int submesh);

	[[nodiscard]] bool
	Holds(int graphIndex) const noexcept
	{
		return graphIndex >= 0 && graphIndex < static_cast<int>(m_Graphs.size());
	}

	[[nodiscard]] Graph&
	At(int graphIndex)
	{
		return m_Graphs[static_cast<size_t>(graphIndex)];
	}

	[[nodiscard]] const Graph&
	At(int graphIndex) const
	{
		return m_Graphs[static_cast<size_t>(graphIndex)];
	}

	[[nodiscard]] std::vector<Graph>&
	All() noexcept
	{
		return m_Graphs;
	}

	/** Whether `submesh` is one this mesh has. */
	[[nodiscard]] bool
	HasSubmesh(int submesh) const noexcept
	{
		return submesh >= 0 && submesh < static_cast<int>(m_GraphForSubmesh.size());
	}

	/** The graph backing `submesh`, or -1 for a submesh this mesh does not have or that has none. */
	[[nodiscard]] int
	ForSubmesh(int submesh) const noexcept;

	/** The graph backing the selected submesh, or -1 when nothing is selected. */
	[[nodiscard]] int
	Current() const noexcept
	{
		return ForSubmesh(m_CurrentSubmesh);
	}

	[[nodiscard]] int
	CurrentSubmesh() const noexcept
	{
		return m_CurrentSubmesh;
	}

	void
	SetCurrentSubmesh(int submesh) noexcept
	{
		m_CurrentSubmesh = submesh;
	}

	/** The graph already open for `materialPath` (file-wise), or -1. */
	[[nodiscard]] int
	FindForPath(const QString& materialPath) const;

	/**
	 * The material files these graphs have open, absolute, in no order. Deleting one behind an open
	 * graph would not stick: the graph still holds it, and the next Save writes it straight back.
	 */
	[[nodiscard]] QStringList
	OpenPaths() const;

	/** Drops every graph's cached parse, for a caller that has just rewritten one on disk. */
	void
	ForgetOnDisk();

private:
	std::vector<Graph> m_Graphs;

	// Submesh index -> index into m_Graphs, or -1.
	std::vector<int> m_GraphForSubmesh;

	int m_CurrentSubmesh = -1;
};
