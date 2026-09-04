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

	constexpr std::string_view c_MaterialArenaBufferName = "scene.materialArenaBuffer"sv;

	// Not rig-prefixed: it is the rig's clip table, but both pose sources index it and the pose pass
	// reads it too (see docs/skinning.md).
	constexpr std::string_view c_ClipBufferName = "scene.clipBuffer"sv;

	// Not skinned-prefixed: a rig is a skeleton and its clips, which no geom owns.
	constexpr std::string_view c_RigBufferName = "scene.rigBuffer"sv;

	// GPU-written like the per-view palette, and imported the same way -- RigFramesPass fills it,
	// nothing uploads it.
	constexpr std::string_view c_BoneAnimTableName = "scene.boneAnimTables"sv;

	constexpr std::string_view c_SkinnedBoneBufferName = "scene.skinnedBoneBuffer"sv;
	constexpr std::string_view c_BoneSampleBufferName  = "scene.boneSampleBuffer"sv;
	constexpr std::string_view c_SkinnedLegBufferName  = "scene.skinnedLegBuffer"sv;
	constexpr std::string_view c_PlantWeightBufferName = "scene.plantWeightBuffer"sv;

	constexpr std::string_view c_InstanceBufferName     = "scene.instanceBuffer"sv;
	constexpr std::string_view c_MeshInstanceBufferName = "scene.meshInstanceBuffer"sv;
	// One arena for every animated placement's playback record, of either tier.
	constexpr std::string_view c_PlaybackArenaBufferName = "scene.playbackBuffer"sv;
	constexpr std::string_view c_SelectedInstancesName   = "scene.selectedInstances"sv;
	// One FootIKLeg per leg of every hero placement in the view, reached through the pose list.
	constexpr std::string_view c_FootIKBufferName = "scene.footIKBuffer"sv;

	// Written by the pose pass rather than uploaded, so neither is in c_Buffers -- see SceneView.
	constexpr std::string_view c_PosedInstancesName = "scene.posedInstances"sv;
	constexpr std::string_view c_BonePaletteName    = "scene.bonePalettes"sv;

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
