#pragma once

#include <string_view>
namespace bgl
{
	// The frame-graph names the scene's buffers are imported under. The graph matches a name by
	// string, so both ends spell it from here: a mistyped one is then a compile error rather than a
	// read that resolves to nothing until draw time.

	constexpr std::string_view c_GeomBufferName    = "scene.geomBuffer"sv;
	constexpr std::string_view c_SubmeshBufferName = "scene.submeshBuffer"sv;
	constexpr std::string_view c_MeshletBufferName = "scene.meshletBuffer"sv;
	// One bound per run of idl::cMeshletsPerGroup meshlets, packed so the static tier's cull streams
	// them rather than gathering them out of the meshlets.
	constexpr std::string_view c_MeshletGroupBufferName = "scene.meshletGroupBuffer"sv;
	constexpr std::string_view c_VertexMapBufferName    = "scene.vertexMapBuffer"sv;
	constexpr std::string_view c_VertexDataBufferName   = "scene.vertexDataBuffer"sv;
	constexpr std::string_view c_IndexBufferName        = "scene.indexBuffer"sv;

	constexpr std::string_view c_MaterialArenaBufferName = "scene.materialArenaBuffer"sv;
	constexpr std::string_view c_ImpostorArenaBufferName = "scene.impostorArenaBuffer"sv;

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
	constexpr std::string_view c_BlendNodeBufferName   = "scene.blendNodeBuffer"sv;
	constexpr std::string_view c_BlendSampleBufferName = "scene.blendSampleBuffer"sv;

	// One GrassLook per CreateGrass, and every attached field's chunks and clumps, each a range a
	// geom owns and DeleteGeom frees.
	constexpr std::string_view c_GrassLookBufferName  = "scene.grassLookBuffer"sv;
	constexpr std::string_view c_GrassChunkBufferName = "scene.grassChunkBuffer"sv;
	constexpr std::string_view c_GrassClumpBufferName = "scene.grassClumpBuffer"sv;

	// One Terrain record per CreateTerrain, and every terrain's node bounds, a range each owns.
	constexpr std::string_view c_TerrainBufferName           = "scene.terrainBuffer"sv;
	constexpr std::string_view c_TerrainNodeBoundsBufferName = "scene.terrainNodeBoundsBuffer"sv;

	// One TerrainGrass record per layer of grass a terrain grows.
	constexpr std::string_view c_TerrainGrassBufferName = "scene.terrainGrassBuffer"sv;

	// One ToonShadingRig per AddToonShadingRig, and the keys its edits blend, a range each rig owns.
	constexpr std::string_view c_ToonShadingRigBufferName    = "scene.toonShadingRigBuffer"sv;
	constexpr std::string_view c_ToonShadingRigKeyBufferName = "scene.toonShadingRigKeyBuffer"sv;

	constexpr std::string_view c_InstanceBufferName     = "scene.instanceBuffer"sv;
	constexpr std::string_view c_MeshInstanceBufferName = "scene.meshInstanceBuffer"sv;
	// One arena for every animated placement's playback record, of either tier.
	constexpr std::string_view c_PlaybackArenaBufferName = "scene.playbackBuffer"sv;
	constexpr std::string_view c_SelectedInstancesName   = "scene.selectedInstances"sv;
	// One FootIKLeg per leg of every hero placement in the view, reached through the pose list.
	constexpr std::string_view c_FootIKBufferName = "scene.footIKBuffer"sv;

	// One entry per placement carrying a blob shadow, dense and CPU-authored like the pose list.
	constexpr std::string_view c_BlobShadowsName = "scene.blobShadows"sv;

	// Every visible placement's grass fields, and one reference per chunk of them grouped by the
	// pixel program they draw through: dense and CPU-authored like the blob list.
	constexpr std::string_view c_GrassDrawsName     = "scene.grassDraws"sv;
	constexpr std::string_view c_GrassChunkRefsName = "scene.grassChunkRefs"sv;

	// Written by the pose pass rather than uploaded, so neither is in c_Buffers -- see SceneView.
	constexpr std::string_view c_PosedInstancesName = "scene.posedInstances"sv;
	constexpr std::string_view c_BonePaletteName    = "scene.bonePalettes"sv;

	// The automatic source's, per view (AutoPoseState): the placements on it, each one's pose slice
	// this frame, the pool's counters, the pose list the camera's cull writes and the requests it
	// collects for the budget's grants.
	constexpr std::string_view c_AutoPlacementsName = "scene.autoPlacements"sv;
	constexpr std::string_view c_InstancePoseName   = "scene.instancePose"sv;
	constexpr std::string_view c_PosePoolName       = "scene.posePool"sv;
	constexpr std::string_view c_AutoPosedName      = "scene.autoPosed"sv;
	constexpr std::string_view c_PoseRequestsName   = "scene.poseRequests"sv;
	constexpr std::string_view c_DominantFramesName = "scene.dominantFrames"sv;

	// The toon shading rigs', per view (ToonShadingRigState): the rigged placement ranges, the pool's
	// counter and the pool of evaluated blocks the forward pass reads.
	constexpr std::string_view c_ToonShadingRigRangesName = "scene.toonShadingRigRanges"sv;
	constexpr std::string_view c_ToonShadingRigPoolName   = "scene.toonShadingRigPool"sv;
	constexpr std::string_view c_ToonShadingRigBlocksName = "scene.toonShadingRigBlocks"sv;

	constexpr std::string_view c_InstanceVisibilityName = "scene.instanceVisibility"sv;
	constexpr std::string_view c_CompactedInstancesName = "scene.compactedInstances"sv;

	// Each placement's level of detail: the word this frame's cull writes, and the one it read.
	constexpr std::string_view c_InstanceLodName         = "scene.instanceLod"sv;
	constexpr std::string_view c_InstanceLodPreviousName = "scene.instanceLodPrevious"sv;

	constexpr std::string_view c_TransparentSortEntriesName = "scene.transparentSortEntries"sv;
	constexpr std::string_view c_TransparentSortCountName   = "scene.transparentSortCount"sv;
	constexpr std::string_view c_SortedTransparentInstancesName =
		"scene.sortedTransparentInstances"sv;
	constexpr std::string_view c_DrawBucketFlagsName = "scene.drawBucketFlags"sv;

	// Scratch a scene collaborator owns, imported into a namespace of its own rather than the
	// scene's. cull.* is per culled frustum, so a view carries one set per CullState.
	constexpr std::string_view c_DrawBucketPrefixSumName =
		"compactedInstances.drawBucketPrefixSumBuffer"sv;
	constexpr std::string_view c_CompactDispatchArgsName =
		"compactedInstances.compactDispatchArgs"sv;
	constexpr std::string_view c_TransparentDispatchArgsName = "transparentSort.dispatchArgs"sv;
	constexpr std::string_view c_CullViewName                = "cull.view"sv;
	constexpr std::string_view c_CullStatsName               = "cull.stats"sv;
}
