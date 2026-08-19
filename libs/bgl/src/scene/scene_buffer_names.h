#pragma once

namespace bgl
{
	// The frame-graph names the scene's buffers are imported under. The graph matches a name by
	// string, so both ends spell it from here: a mistyped one is then a compile error rather than a
	// read that resolves to nothing until draw time.

	constexpr std::string_view c_SubmeshBufferName    = "scene.submeshBuffer"sv;
	constexpr std::string_view c_MeshletBufferName    = "scene.meshletBuffer"sv;
	constexpr std::string_view c_VertexMapBufferName  = "scene.vertexMapBuffer"sv;
	constexpr std::string_view c_VertexDataBufferName = "scene.vertexDataBuffer"sv;
	constexpr std::string_view c_IndexBufferName      = "scene.indexBuffer"sv;

	constexpr std::string_view c_PbrMaterialBufferName   = "scene.pbrMaterialBuffer"sv;
	constexpr std::string_view c_LooseMaterialBufferName = "scene.looseMaterialBuffer"sv;

	constexpr std::string_view c_VatGeomBufferName   = "scene.vatGeomBuffer"sv;
	constexpr std::string_view c_VatClipBufferName   = "scene.vatClipBuffer"sv;
	constexpr std::string_view c_VatColumnBufferName = "scene.vatColumnBuffer"sv;

	constexpr std::string_view c_InstanceBufferName     = "scene.instanceBuffer"sv;
	constexpr std::string_view c_MeshInstanceBufferName = "scene.meshInstanceBuffer"sv;
	constexpr std::string_view c_VatStateBufferName     = "scene.vatStateBuffer"sv;
	constexpr std::string_view c_SelectedInstancesName  = "scene.selectedInstances"sv;

	constexpr std::string_view c_InstanceVisibilityName = "scene.instanceVisibility"sv;
	constexpr std::string_view c_CompactedInstancesName = "scene.compactedInstances"sv;

	constexpr std::string_view c_TransparentSortEntriesName = "scene.transparentSortEntries"sv;
	constexpr std::string_view c_TransparentSortCountName   = "scene.transparentSortCount"sv;
	constexpr std::string_view c_SortedTransparentInstancesName =
		"scene.sortedTransparentInstances"sv;

	// Scratch a scene collaborator owns, imported into a namespace of its own rather than the
	// scene's. cull.* is per culled frustum, so a view carries one set per CullState.
	constexpr std::string_view c_PsoPrefixSumName = "compactedInstances.psoPrefixSumBuffer"sv;
	constexpr std::string_view c_CompactDispatchArgsName =
		"compactedInstances.compactDispatchArgs"sv;
	constexpr std::string_view c_TransparentDispatchArgsName = "transparentSort.dispatchArgs"sv;
	constexpr std::string_view c_CullViewName                = "cull.view"sv;
	constexpr std::string_view c_CullStatsName               = "cull.stats"sv;
}
