// Held in a unique_ptr, whose deleter needs sizeof(T) -- invisible to include-cleaner.
#include "MaterialGraphSet.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"  // IWYU pragma: keep
#include "Windows/MaterialEditor/MaterialGraphScene.h"  // IWYU pragma: keep

#include "Windows/MaterialEditor/material_io.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <qcontainerfwd.h>
#include <qobject.h>

void
MaterialGraphSet::Reset(int submeshCount)
{
	m_Graphs.clear();
	m_GraphForSubmesh.assign(static_cast<size_t>(std::max(submeshCount, 0)), -1);
	m_CurrentSubmesh = -1;
}

void
MaterialGraphSet::Clear()
{
	m_Graphs.clear();
	m_GraphForSubmesh.clear();
	m_CurrentSubmesh = -1;
}

int
MaterialGraphSet::Add(int submesh)
{
	const int graphIndex = static_cast<int>(m_Graphs.size());
	m_Graphs.emplace_back().submeshes.push_back(static_cast<uint32_t>(submesh));
	m_GraphForSubmesh[static_cast<size_t>(submesh)] = graphIndex;

	return graphIndex;
}

void
MaterialGraphSet::Share(int graphIndex, int submesh)
{
	At(graphIndex).submeshes.push_back(static_cast<uint32_t>(submesh));
	m_GraphForSubmesh[static_cast<size_t>(submesh)] = graphIndex;
}

int
MaterialGraphSet::ForSubmesh(int submesh) const noexcept
{
	if (!HasSubmesh(submesh))
		return -1;

	return m_GraphForSubmesh[static_cast<size_t>(submesh)];
}

int
MaterialGraphSet::FindForPath(const QString& materialPath) const
{
	for (size_t i = 0; i < m_Graphs.size(); ++i)
		if (editor::IsSameMaterialFile(m_Graphs[i].materialPath, materialPath))
			return static_cast<int>(i);

	return -1;
}

QStringList
MaterialGraphSet::OpenPaths() const
{
	auto paths = QStringList();
	for (const Graph& graph : m_Graphs)
		if (!graph.materialPath.isEmpty())
			paths << graph.materialPath;

	return paths;
}

void
MaterialGraphSet::ForgetOnDisk()
{
	for (Graph& graph : m_Graphs) graph.onDisk.Forget();
}
