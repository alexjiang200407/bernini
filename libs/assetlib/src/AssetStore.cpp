#include <assetlib/AssetStore.h>

#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/env_resolve.h>
#include <assetlib/image_io.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceStamp.h>

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

namespace assetlib
{
	AssetStore::AssetStore(std::filesystem::path dataRoot) :
		m_DataRoot(std::move(dataRoot)),
		m_Files(std::make_shared<const core::file::LooseFileSystem>(m_DataRoot))
	{
		// Here and not at each use: a mount over a directory that is not there enumerates empty
		// rather than failing, so a mistyped root would otherwise read as a project with nothing in
		// it -- a scan reporting no assets, a prune reporting nothing to sweep.
		if (!std::filesystem::is_directory(m_DataRoot))
			core::throw_runtime_error(
				"assetlib::AssetStore: '{}' is not a directory",
				m_DataRoot.string());
	}

	AssetStore::AssetStore(
		std::filesystem::path                          dataRoot,
		std::shared_ptr<const core::file::IFileSystem> files) :
		m_DataRoot(std::move(dataRoot)), m_Files(std::move(files))
	{
		if (!m_Files)
			core::throw_runtime_error("assetlib::AssetStore: a source must have somewhere to read");
	}

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

	ImageData
	AssetStore::LoadTexture(std::string_view path, Ktx2Decode decode, uint32_t maxDim) const
	{
		return loadKTX2(*m_Files, path, decode, maxDim);
	}

	ImageData
	AssetStore::LoadTexture(std::string_view path) const
	{
		return loadKTX2(*m_Files, path);
	}

	ImageData
	AssetStore::LoadTexturePreview(std::string_view path, uint32_t maxDim) const
	{
		return loadKTX2Preview(*m_Files, path, maxDim);
	}

	SourceStamp
	AssetStore::StampOf(std::string_view path) const
	{
		return stampOf(*m_Files, path);
	}

	bool
	AssetStore::BakeIsStale(const BMaterial& material) const
	{
		return bakeIsStale(material, *m_Files);
	}

	bool
	AssetStore::DrawsLoose(const BMaterial& material) const
	{
		return drawsLoose(material, *m_Files);
	}

	bool
	AssetStore::VatIsStale(const BVat& vat) const
	{
		return vatIsStale(vat, *m_Files);
	}

	bool
	AssetStore::IsSkyBakeStale(const BSky& sky) const
	{
		return isSkyBakeStale(sky, *m_Files);
	}

	bool
	AssetStore::IsEnvLightingBakeStale(const BEnvLighting& lighting) const
	{
		return isEnvLightingBakeStale(lighting, *m_Files);
	}

	const std::string&
	AssetStore::EnvMapToDraw(const EnvMapRoute& route) const
	{
		return envMapToDraw(route, *m_Files);
	}

	ResolvedEnvironment
	AssetStore::ResolveEnvironment(const std::filesystem::path& benvPath) const
	{
		return resolveEnvironment(benvPath, *m_Files);
	}
}
