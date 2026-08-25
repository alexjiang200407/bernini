#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib/skeleton.h>
#include <assetlib/vat_bake.h>

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>

#include "mounted_io.h"

namespace assetlib
{
	BMesh
	AssetStore::LoadMesh(std::string_view path) const
	{
		return Load<BMesh>(path);
	}

	MeshRefs
	AssetStore::LoadMeshRefs(std::string_view path) const
	{
		return loadMeshRefs(*m_Files, path);
	}

	Skeleton
	AssetStore::LoadSkeleton(std::string_view path) const
	{
		return Load<Skeleton>(path);
	}

	AnimationSet
	AssetStore::LoadAnimations(std::string_view path) const
	{
		return Load<AnimationSet>(path);
	}

	std::string
	AssetStore::LoadAnimationSkeletonPath(std::string_view path) const
	{
		return loadAnimationSkeletonPath(*m_Files, path);
	}

	BMaterial
	AssetStore::LoadMaterial(std::string_view path) const
	{
		return Load<BMaterial>(path);
	}

	BEnv
	AssetStore::LoadEnv(std::string_view path) const
	{
		return Load<BEnv>(path);
	}

	BSky
	AssetStore::LoadSky(std::string_view path) const
	{
		return Load<BSky>(path);
	}

	BEnvLighting
	AssetStore::LoadEnvLighting(std::string_view path) const
	{
		return Load<BEnvLighting>(path);
	}

	BVat
	AssetStore::LoadVat(std::string_view path) const
	{
		return Load<BVat>(path);
	}

	BVat
	AssetStore::LoadVatTables(std::string_view path) const
	{
		return loadVatTables(*m_Files, path);
	}

	VatRefs
	AssetStore::LoadVatRefs(std::string_view path) const
	{
		return loadVatRefs(*m_Files, path);
	}
}
