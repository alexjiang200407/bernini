#include <assetlib/AssetStore.h>

#include <assetlib/bmesh_io.h>
#include <assetlib/bvat_io.h>
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
		return load(*m_Files, path);
	}

	MeshRefs
	AssetStore::LoadMeshRefs(std::string_view path) const
	{
		return loadMeshRefs(*m_Files, path);
	}

	Skeleton
	AssetStore::LoadSkeleton(std::string_view path) const
	{
		return loadSkeleton(*m_Files, path);
	}

	AnimationSet
	AssetStore::LoadAnimations(std::string_view path) const
	{
		return loadAnimations(*m_Files, path);
	}

	std::string
	AssetStore::LoadAnimationSkeletonPath(std::string_view path) const
	{
		return loadAnimationSkeletonPath(*m_Files, path);
	}

	BMaterial
	AssetStore::LoadMaterial(std::string_view path) const
	{
		return loadMaterial(*m_Files, path);
	}

	BEnv
	AssetStore::LoadEnv(std::string_view path) const
	{
		return loadEnv(*m_Files, path);
	}

	BSky
	AssetStore::LoadSky(std::string_view path) const
	{
		return loadSky(*m_Files, path);
	}

	BEnvLighting
	AssetStore::LoadEnvLighting(std::string_view path) const
	{
		return loadEnvLighting(*m_Files, path);
	}

	BVat
	AssetStore::LoadVat(std::string_view path) const
	{
		return loadVat(*m_Files, path);
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
