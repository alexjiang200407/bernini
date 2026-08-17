#include <gamelib/AssetManager.h>

#include <gamelib/vat_freshness.h>

#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/image_io.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/ImageData.h>
#include <core/err/util.h>

namespace game
{
	namespace
	{
		// The asset says what the material *is* (its glTF-shaped alpha mode); the renderer says which
		// pass composites it. They are separate enums because assetlib cannot link bgl, and separate
		// concepts because a layer is free to grow buckets no material ever authors. gamelib is the
		// only library that links both, so this is the one place they meet.
		bgl::LayerType
		ToLayerType(assetlib::AlphaMode mode, bool hashedAsBlend)
		{
			switch (mode)
			{
			case assetlib::AlphaMode::kMask:
				return bgl::LayerType::kMask;
			case assetlib::AlphaMode::kBlend:
				return bgl::LayerType::kBlend;
			case assetlib::AlphaMode::kHashed:
				return hashedAsBlend ? bgl::LayerType::kBlend : bgl::LayerType::kHashed;
			case assetlib::AlphaMode::kOpaque:
				break;
			}
			return bgl::LayerType::kOpaque;
		}

	}

	// The order MaterialRecord::textures parallels: the baked triplet, or the nine authoring routes.
	// One order per case, in one place, so the record's texture references and the desc it rebuilds
	// can never fall out of step.
	std::vector<std::string>
	MaterialTextures(const assetlib::BMaterial& material, const bool loose)
	{
		const assetlib::PbrParams& pbr = material.pbr;

		if (loose)
		{
			auto paths = std::vector<std::string>(assetlib::c_LooseChannelCount);
			for (size_t i = 0; i < assetlib::c_LooseChannelCount; ++i)
				paths[i] = pbr.routes[i].texture;
			return paths;
		}

		return { pbr.baseColorTexture, pbr.normalTexture, pbr.ormTexture };
	}

	AssetManager::AssetManager(
		bgl::SceneRef         scene,
		std::filesystem::path dataRoot,
		AssetManagerOptions   options) :
		AssetManager(std::move(scene), assetlib::AssetStore(std::move(dataRoot)), options)
	{}

	AssetManager::AssetManager(
		bgl::SceneRef        scene,
		assetlib::AssetStore store,
		AssetManagerOptions  options) :
		m_Scene(std::move(scene)), m_Store(std::move(store)), m_Options(options)
	{
		// Held, not borrowed. The destructor hands every asset back to the scene, so the scene has to
		// still be there -- and with a bare reference that was only true if the caller happened to
		// declare them in the right order.
		if (!m_Scene)
			throw bgl::SceneError("AssetManager requires a valid Scene");
	}

	AssetManager::~AssetManager()
	{
		// Tear down top-down, ignoring the counts: whatever is still held is being abandoned, and the
		// order is what matters. Each level's deletion precondition is that nothing above it survives
		// -- an instance drawing a geom, a submesh bound to a material, a material routing a texture --
		// so instances go first and textures last.
		try
		{
			for (const auto& [slot, instance] : m_Instances)
				instance.view->DeleteMeshInstance(instance.handle);
			m_Instances.clear();

			for (const auto& [slot, geom] : m_Geoms) m_Scene->DeleteGeom(geom.handle);
			m_Geoms.clear();

			for (const auto& [key, material] : m_Materials)
				m_Scene->DeleteMaterial(material.handle);
			m_Materials.clear();

			for (const auto& [slot, texture] : m_Textures)
				m_Scene->DeleteTextureAsset(texture.handle);
			m_Textures.clear();
		}
		catch (const std::exception& e)
		{
			// A destructor cannot throw, and there is nothing left to salvage: the scene is either
			// being torn down with us or has already outlived what it held.
			logger::warn("~AssetManager: failed to release assets: {}", e.what());
		}
	}

	// --- Acquire ----------------------------------------------------------------------------------

	bgl::TextureAssetHandle
	AssetManager::AcquireTexture(std::string_view relPath, TexturePrefetch* prefetch)
	{
		// An absent map is not an error: the scene substitutes its own default (white, or a flat
		// normal) for an invalid handle.
		if (relPath.empty())
			return {};

		if (const auto it = m_TextureByPath.find(relPath); it != m_TextureByPath.end())
		{
			TextureRecord& record = m_Textures.at(it->second);
			++record.refCount;
			return record.handle;
		}

		// Someone decoded this for us off the render thread. Take it; only the upload has to be here.
		// A supplied prefetch is authoritative: a path it lacks is one whose decode already failed
		// and was reported there, and re-reading the file would put the decode back on this thread --
		// the very cost the prefetch exists to keep off it. The invalid handle reads as "absent", so
		// the scene substitutes its default map.
		auto decoded = assetlib::ImageData();
		if (prefetch != nullptr)
		{
			const auto it = prefetch->find(relPath);
			if (it == prefetch->end())
			{
				logger::warn(
					"AcquireTexture: '{}' is not in the prefetch; using the default map",
					relPath);
				return {};
			}

			decoded = std::move(it->second);
			prefetch->erase(it);
		}

		auto key = std::string(relPath);

		const bgl::TextureAssetHandle handle = m_Scene->AddTextureAsset(
			prefetch != nullptr ? std::move(decoded) : m_Store.LoadTexture(key),
			key);

		m_TextureByPath.emplace(key, handle.textureSlot.index);
		m_Textures.emplace(handle.textureSlot.index, TextureRecord{ std::move(key), handle, 1 });

		return handle;
	}

	AssetManager::Environment
	AssetManager::AcquireEnvironment(std::string_view relPath)
	{
		const assetlib::BEnv env = m_Store.LoadEnv(relPath);

		const auto acquireRoute = [this](const assetlib::EnvMapRoute& route) {
			return AcquireTexture(m_Store.EnvMapToDraw(route));
		};

		Environment out;

		if (!env.sky.empty())
		{
			const assetlib::BSky sky = m_Store.LoadSky(env.sky);
			out.skybox               = acquireRoute(sky.sky);
			out.skyMipLevel          = sky.mipLevel;
			out.skyRotationY         = sky.rotationY;
		}

		if (!env.lighting.empty())
		{
			const assetlib::BEnvLighting lighting = m_Store.LoadEnvLighting(env.lighting);
			out.prefilter                         = acquireRoute(lighting.prefilter);
			out.irradiance                        = acquireRoute(lighting.irradiance);
			out.exposure                          = lighting.exposure;
		}

		return out;
	}

	void
	AssetManager::ReleaseEnvironment(const Environment& environment)
	{
		ReleaseTexture(environment.irradiance);
		ReleaseTexture(environment.prefilter);
		ReleaseTexture(environment.skybox);
	}

	bgl::MaterialHandle
	AssetManager::AcquireMaterial(std::string_view relPath, TexturePrefetch* prefetch)
	{
		if (relPath.empty())
			return {};

		if (const auto it = m_MaterialByPath.find(relPath); it != m_MaterialByPath.end())
		{
			MaterialRecord& record = m_Materials.at(it->second);
			++record.refCount;
			return record.handle;
		}

		auto key = std::string(relPath);

		// Load first, in its own statement: the order in which a call's arguments are evaluated is
		// unspecified, so passing `m_Store.LoadMaterial(key)` alongside `std::move(key)` lets the
		// compiler move `key` out from under the path it is supposed to build.
		const assetlib::BMaterial material = m_Store.LoadMaterial(key);

		return CreateMaterial(material, std::move(key), prefetch);
	}

	bgl::MaterialHandle
	AssetManager::CreateMaterial(
		const assetlib::BMaterial& material,
		std::string                key,
		TexturePrefetch*           prefetch)
	{
		if (material.shadingModel != assetlib::ShadingModel::kPbr)
			throw bgl::SceneError(
				"AssetManager: shading model " +
				std::to_string(static_cast<uint32_t>(material.shadingModel)) +
				" is not supported by the renderer");

		// The disk decides: a triplet that is missing or older than the sources it was composited from
		// cannot be sampled, so the material falls back to the routes that produced it -- when those
		// are still there to fall back to.
		const bool loose = m_Store.DrawsLoose(material);

		// Acquire the textures first: the desc the scene needs is built out of their handles.
		const std::vector<std::string> paths = MaterialTextures(material, loose);

		auto textures = std::vector<bgl::TextureAssetHandle>(paths.size());
		for (size_t i = 0; i < paths.size(); ++i) textures[i] = AcquireTexture(paths[i], prefetch);

		auto record     = MaterialRecord();
		record.key      = key;
		record.source   = material;
		record.textures = std::move(textures);
		record.loose    = loose;
		record.refCount = 1;

		record.handle = loose ? m_Scene->CreateLoosePbrMaterial(LooseDesc(record)) :
		                        m_Scene->CreatePbrMaterial(BakedDesc(record));

		const uint64_t recordKey = MaterialKey(record.handle);
		if (!key.empty())
			m_MaterialByPath.emplace(std::move(key), recordKey);

		const bgl::MaterialHandle handle = record.handle;
		m_Materials.emplace(recordKey, std::move(record));

		return handle;
	}

	bgl::GeomHandle
	AssetManager::AcquireMesh(std::string_view relPath, uint32_t meshIndex)
	{
		// A .bmesh holds several meshes, so the file alone does not identify geometry.
		const auto key = std::format("{}#{}", relPath, meshIndex);

		if (const auto it = m_GeomByPath.find(key); it != m_GeomByPath.end())
		{
			GeomRecord& record = m_Geoms.at(it->second);
			++record.refCount;
			return record.handle;
		}

		const assetlib::BMesh mesh = m_Store.LoadMesh(relPath);

		if (meshIndex >= mesh.meshes.size())
		{
			throw std::runtime_error(
				std::format(
					"AssetManager: mesh index {} out of range in '{}'",
					meshIndex,
					relPath));
		}

		const assetlib::Mesh& entry = mesh.meshes[meshIndex];

		// AddStaticMeshGeom wants a list parallel to the .bmesh's own material list, but only the
		// materials *this* mesh's submeshes name are worth loading -- another mesh in the same file
		// may name others. Anything left invalid renders unlit, which is what AddStaticMeshGeom does with
		// an out-of-range material index anyway.
		auto materials = std::vector<bgl::MaterialHandle>(mesh.materials.size());

		// One reference per submesh, not per distinct material: the geom holds a reference for each
		// submesh bound to a material, so releasing it drops exactly as many as it took.
		auto submeshMaterials = std::vector<bgl::MaterialHandle>(entry.submeshCount);

		for (uint32_t i = 0; i < entry.submeshCount; ++i)
		{
			const uint32_t index = mesh.submeshes[entry.firstSubmesh + i].material;
			if (index >= mesh.materials.size())
				continue;

			const bgl::MaterialHandle handle = AcquireMaterial(mesh.materials[index]);

			materials[index]    = handle;
			submeshMaterials[i] = handle;
		}

		auto record             = GeomRecord();
		record.handle           = m_Scene->AddStaticMeshGeom(mesh, meshIndex, materials);
		record.key              = key;
		record.submeshMaterials = std::move(submeshMaterials);
		record.refCount         = 1;

		const uint32_t slot = record.handle.handle.index;

		m_GeomByPath.emplace(key, slot);

		const bgl::GeomHandle handle = record.handle;
		m_Geoms.emplace(slot, std::move(record));

		return handle;
	}

	AssetManager::VatMesh
	AssetManager::AcquireVatMesh(
		std::string_view relPath,
		std::string_view animationsRelPath,
		uint32_t         meshIndex)
	{
		// Its own keyspace beside AcquireMesh's "path#index": the same mesh may be live as static
		// and as VAT geometry at once, and they are different uploads.
		const auto key = std::format("{}#{}#vat", relPath, meshIndex);

		if (const auto it = m_GeomByPath.find(key); it != m_GeomByPath.end())
		{
			GeomRecord& record = m_Geoms.at(it->second);
			core::throw_runtime_error_if(
				record.vatAnimations != assetlib::normalizePath(animationsRelPath),
				"AssetManager: '{}' is live with clips from '{}'; release it to zero before "
				"acquiring with '{}'",
				key,
				record.vatAnimations,
				animationsRelPath);
			++record.refCount;
			return VatMesh{ record.handle, record.vatClips };
		}

		const auto bvatRel = assetlib::vatPathFor(relPath, animationsRelPath);
		const auto vat     = EnsureVatBaked(m_Store, relPath, animationsRelPath);

		const assetlib::BMesh mesh = m_Store.LoadMesh(relPath);

		core::throw_runtime_error_if(
			meshIndex >= mesh.meshes.size(),
			"AssetManager: mesh index {} out of range in '{}'",
			meshIndex,
			relPath);

		const assetlib::Mesh& entry = mesh.meshes[meshIndex];
		core::throw_runtime_error_if(
			size_t(entry.firstSubmesh) + entry.submeshCount > vat.columns.size(),
			"AssetManager: '{}' does not cover mesh {}'s submeshes",
			bvatRel.string(),
			meshIndex);

		// Everything taken below is given back if any later step throws: a failed acquire owns
		// nothing.
		auto acquiredTextures  = std::vector<bgl::TextureAssetHandle>();
		auto acquiredMaterials = std::vector<bgl::MaterialHandle>();
		try
		{
			// The pair is keyed on the container and the bake it holds: two meshes of one .bvat
			// share the uploads, but a rebake from another .banim must not inherit the old pixels.
			acquiredTextures.push_back(AddEmbeddedTexture(
				bvatRel.string() + "#" + vat.animations + "#positions",
				assetlib::decodeKTX2(vat.positionsKtx2)));
			acquiredTextures.push_back(AddEmbeddedTexture(
				bvatRel.string() + "#" + vat.animations + "#normals",
				assetlib::decodeKTX2(vat.normalsKtx2)));

			auto materials        = std::vector<bgl::MaterialHandle>(mesh.materials.size());
			auto submeshMaterials = std::vector<bgl::MaterialHandle>(entry.submeshCount);

			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const uint32_t index = mesh.submeshes[entry.firstSubmesh + i].material;
				if (index >= mesh.materials.size())
					continue;

				const bgl::MaterialHandle handle = AcquireMaterial(mesh.materials[index]);
				acquiredMaterials.push_back(handle);

				materials[index]    = handle;
				submeshMaterials[i] = handle;
			}

			auto desc      = bgl::VatGeomDesc();
			desc.positions = acquiredTextures[0];
			desc.normals   = acquiredTextures[1];
			desc.boundsMin = vat.boundsMin;
			desc.boundsMax = vat.boundsMax;

			auto clipInfo = std::vector<VatClipInfo>();
			clipInfo.reserve(vat.clips.size());
			desc.clips.reserve(vat.clips.size());
			for (const assetlib::VatClip& clip : vat.clips)
			{
				desc.clips.push_back(
					{ clip.firstRow, clip.frameCount, clip.sampleRate, clip.loop != 0 });
				clipInfo.push_back(
					{ std::string(vat.stringPool.at(clip.nameOffset)),
				      clip.frameCount,
				      clip.sampleRate,
				      clip.duration,
				      clip.loop != 0 });
			}

			desc.columnBases.reserve(entry.submeshCount);
			for (uint32_t i = 0; i < entry.submeshCount; ++i)
				desc.columnBases.push_back(vat.columns[entry.firstSubmesh + i].columnBase);

			auto record             = GeomRecord();
			record.handle           = m_Scene->AddVatMeshGeom(mesh, meshIndex, materials, desc);
			record.key              = key;
			record.submeshMaterials = std::move(submeshMaterials);
			record.vatTextures      = acquiredTextures;
			record.vatClips         = clipInfo;
			record.vatAnimations    = vat.animations;
			record.refCount         = 1;

			const uint32_t slot = record.handle.handle.index;
			m_GeomByPath.emplace(key, slot);

			const bgl::GeomHandle handle = record.handle;
			m_Geoms.emplace(slot, std::move(record));

			return VatMesh{ handle, std::move(clipInfo) };
		}
		catch (...)
		{
			for (const bgl::MaterialHandle material : acquiredMaterials) ReleaseMaterial(material);
			for (const bgl::TextureAssetHandle texture : acquiredTextures) ReleaseTexture(texture);
			throw;
		}
	}

	bgl::TextureAssetHandle
	AssetManager::AddEmbeddedTexture(std::string key, assetlib::ImageData image)
	{
		if (const auto it = m_TextureByPath.find(key); it != m_TextureByPath.end())
		{
			TextureRecord& record = m_Textures.at(it->second);
			++record.refCount;
			return record.handle;
		}

		const bgl::TextureAssetHandle handle = m_Scene->AddTextureAsset(std::move(image), key);

		m_TextureByPath.emplace(key, handle.textureSlot.index);
		m_Textures.emplace(handle.textureSlot.index, TextureRecord{ std::move(key), handle, 1 });

		return handle;
	}

	// --- Procedural geometry ----------------------------------------------------------------------

	bgl::GeomHandle
	AssetManager::CreateCube(bgl::MaterialHandle material)
	{
		AddMaterialRef(material);

		auto record             = GeomRecord();
		record.handle           = m_Scene->AddCubeGeom(material);
		record.submeshMaterials = { material };  // procedural geometry is a single submesh
		record.refCount         = 1;

		const uint32_t        slot   = record.handle.handle.index;
		const bgl::GeomHandle handle = record.handle;

		m_Geoms.emplace(slot, std::move(record));
		return handle;
	}

	bgl::GeomHandle
	AssetManager::CreateSphere(
		uint32_t            xSegments,
		uint32_t            ySegments,
		float               radius,
		bgl::MaterialHandle material)
	{
		AddMaterialRef(material);

		auto record             = GeomRecord();
		record.handle           = m_Scene->AddSphereGeom(xSegments, ySegments, radius, material);
		record.submeshMaterials = { material };
		record.refCount         = 1;

		const uint32_t        slot   = record.handle.handle.index;
		const bgl::GeomHandle handle = record.handle;

		m_Geoms.emplace(slot, std::move(record));
		return handle;
	}

	// --- Instances --------------------------------------------------------------------------------

	bgl::MeshInstanceHandle
	AssetManager::CreateInstance(
		bgl::SceneViewRef view,
		bgl::GeomHandle   geom,
		const glm::mat4&  transform)
	{
		if (!view)
			throw bgl::SceneError("CreateInstance requires a valid SceneView");

		const auto it = m_Geoms.find(geom.handle.index);
		if (it == m_Geoms.end() || !m_Scene->IsGeomAlive(geom))
		{
			throw bgl::SceneError(
				"GeomHandle passed to CreateInstance is not owned by this AssetManager, or has "
				"expired");
		}

		const bgl::MeshInstanceHandle instance = view->CreateStaticMeshInstance(geom, transform);

		RegisterInstance(std::move(view), geom.handle.index, instance);

		return instance;
	}

	bgl::MeshInstanceHandle
	AssetManager::CreateVatInstance(
		bgl::SceneViewRef           view,
		bgl::GeomHandle             geom,
		const glm::mat4&            transform,
		const bgl::VatInstanceDesc& desc)
	{
		if (!view)
			throw bgl::SceneError("CreateVatInstance requires a valid SceneView");

		const auto it = m_Geoms.find(geom.handle.index);
		if (it == m_Geoms.end() || !m_Scene->IsGeomAlive(geom))
		{
			throw bgl::SceneError(
				"GeomHandle passed to CreateVatInstance is not owned by this AssetManager, or has "
				"expired");
		}

		const bgl::MeshInstanceHandle instance = view->CreateVatMeshInstance(geom, transform, desc);

		RegisterInstance(std::move(view), geom.handle.index, instance);

		return instance;
	}

	void
	AssetManager::RegisterInstance(
		bgl::SceneViewRef       view,
		uint32_t                geomSlot,
		bgl::MeshInstanceHandle instance)
	{
		GeomRecord& geom = m_Geoms.at(geomSlot);

		// The instance's reference is what keeps the geometry alive while it is being drawn.
		++geom.refCount;

		const InstanceKey key{ view.Get(), instance.handle.index };

		auto record      = InstanceRecord();
		record.handle    = instance;
		record.view      = std::move(view);
		record.geomSlot  = geomSlot;
		record.overrides = std::vector<bgl::MaterialHandle>(geom.submeshMaterials.size());

		m_Instances.emplace(key, std::move(record));
	}

	void
	AssetManager::DestroyInstance(bgl::SceneViewRef view, bgl::MeshInstanceHandle instance)
	{
		const auto it = m_Instances.find(InstanceKey{ view.Get(), instance.handle.index });
		if (it == m_Instances.end())
			return;

		const uint32_t                         geomSlot  = it->second.geomSlot;
		const std::vector<bgl::MaterialHandle> overrides = std::move(it->second.overrides);

		it->second.view->DeleteMeshInstance(it->second.handle);
		m_Instances.erase(it);

		// The instance is gone, so nothing wears these any more.
		for (const bgl::MaterialHandle material : overrides) ReleaseMaterial(material);

		DropGeomRef(geomSlot);
	}

	// --- Release ----------------------------------------------------------------------------------

	void
	AssetManager::ReleaseGeom(bgl::GeomHandle geom)
	{
		if (m_Geoms.contains(geom.handle.index))
			DropGeomRef(geom.handle.index);
	}

	void
	AssetManager::DropGeomRef(uint32_t geomSlot)
	{
		const auto it = m_Geoms.find(geomSlot);
		if (it == m_Geoms.end())
			return;

		GeomRecord& record = it->second;

		assert(record.refCount > 0 && "AssetManager: geom reference count underflow");
		if (--record.refCount > 0)
			return;

		// Zero references means no instance is drawing it -- an instance holds one -- so DeleteGeom's
		// precondition is met by construction.
		DestroyGeom(record);

		if (!record.key.empty())
			m_GeomByPath.erase(record.key);

		m_Geoms.erase(it);
	}

	void
	AssetManager::DestroyGeom(GeomRecord& record)
	{
		// Geometry first, then its materials. Releasing a material may delete it, and a material may
		// not be deleted while a submesh is still bound to it -- deleting the geom is what unbinds
		// them.
		m_Scene->DeleteGeom(record.handle);

		for (const bgl::MaterialHandle material : record.submeshMaterials)
			ReleaseMaterial(material);

		// After the geom, like the materials: a VAT record holds the pair's descriptors until
		// DeleteGeom retires it.
		for (const bgl::TextureAssetHandle texture : record.vatTextures) ReleaseTexture(texture);
	}

	void
	AssetManager::AddMaterialRef(bgl::MaterialHandle material)
	{
		if (!material.IsValid())
			return;

		if (const auto it = m_Materials.find(MaterialKey(material)); it != m_Materials.end())
			++it->second.refCount;
	}

	void
	AssetManager::ReleaseMaterial(bgl::MaterialHandle material)
	{
		if (!material.IsValid())
			return;

		const auto it = m_Materials.find(MaterialKey(material));
		if (it == m_Materials.end())
			return;

		MaterialRecord& record = it->second;

		assert(record.refCount > 0 && "AssetManager: material reference count underflow");
		if (--record.refCount > 0)
			return;

		// Material first, then its textures, for the same reason DestroyGeom deletes the geom first: a
		// texture may not be deleted while a material still routes it.
		m_Scene->DeleteMaterial(record.handle);

		for (const bgl::TextureAssetHandle texture : record.textures) ReleaseTexture(texture);

		if (!record.key.empty())
			m_MaterialByPath.erase(record.key);

		m_Materials.erase(it);
	}

	void
	AssetManager::ReleaseTexture(bgl::TextureAssetHandle texture)
	{
		if (!texture.textureSlot)
			return;

		const auto it = m_Textures.find(texture.textureSlot.index);
		if (it == m_Textures.end())
			return;

		TextureRecord& record = it->second;

		assert(record.refCount > 0 && "AssetManager: texture reference count underflow");
		if (--record.refCount > 0)
			return;

		m_Scene->DeleteTextureAsset(record.handle);

		m_TextureByPath.erase(record.key);
		m_Textures.erase(it);
	}

	// --- Swapping ---------------------------------------------------------------------------------

	void
	AssetManager::SetSubmeshMaterial(
		bgl::GeomHandle  geom,
		uint32_t         submeshIndex,
		std::string_view materialRelPath)
	{
		const auto it = m_Geoms.find(geom.handle.index);
		if (it == m_Geoms.end())
			throw bgl::SceneError(
				"GeomHandle passed to SetSubmeshMaterial is not owned by this AssetManager");

		GeomRecord& record = it->second;
		if (submeshIndex >= record.submeshMaterials.size())
			throw bgl::SceneError("submeshIndex passed to SetSubmeshMaterial is out of range");

		// Acquire before releasing: rebinding a submesh to the material it already has must not drop
		// the count to zero and delete it out from under itself.
		const bgl::MaterialHandle replacement = AcquireMaterial(materialRelPath);
		const bgl::MaterialHandle previous    = record.submeshMaterials[submeshIndex];

		m_Scene->SetSubmeshMaterial(geom, submeshIndex, replacement);
		record.submeshMaterials[submeshIndex] = replacement;

		ReleaseMaterial(previous);
	}

	void
	AssetManager::SetInstanceSubmeshMaterial(
		bgl::SceneViewRef       view,
		bgl::MeshInstanceHandle instance,
		uint32_t                submeshIndex,
		std::string_view        materialRelPath)
	{
		const auto it = m_Instances.find(InstanceKey{ view.Get(), instance.handle.index });
		if (it == m_Instances.end())
			throw bgl::SceneError(
				"MeshInstanceHandle passed to SetInstanceSubmeshMaterial is not owned by this "
				"AssetManager");

		InstanceRecord& record = it->second;
		if (submeshIndex >= record.overrides.size())
			throw bgl::SceneError(
				"submeshIndex passed to SetInstanceSubmeshMaterial is out of range");

		// Acquire before releasing: overriding with the material already worn must not drop the count
		// to zero and delete it out from under itself.
		const bgl::MaterialHandle replacement = AcquireMaterial(materialRelPath);
		const bgl::MaterialHandle previous    = record.overrides[submeshIndex];

		record.view->SetSubmeshMaterialOverride(record.handle, submeshIndex, replacement);
		record.overrides[submeshIndex] = replacement;

		ReleaseMaterial(previous);
	}

	void
	AssetManager::ClearInstanceSubmeshMaterial(
		bgl::SceneViewRef       view,
		bgl::MeshInstanceHandle instance,
		uint32_t                submeshIndex)
	{
		const auto it = m_Instances.find(InstanceKey{ view.Get(), instance.handle.index });
		if (it == m_Instances.end())
			throw bgl::SceneError(
				"MeshInstanceHandle passed to ClearInstanceSubmeshMaterial is not owned by this "
				"AssetManager");

		InstanceRecord& record = it->second;
		if (submeshIndex >= record.overrides.size())
			throw bgl::SceneError(
				"submeshIndex passed to ClearInstanceSubmeshMaterial is out of range");

		const bgl::MaterialHandle previous = record.overrides[submeshIndex];

		record.view->ClearSubmeshMaterialOverride(record.handle, submeshIndex);
		record.overrides[submeshIndex] = {};

		ReleaseMaterial(previous);
	}

	void
	AssetManager::SetMaterialTexture(
		bgl::MaterialHandle material,
		TextureSlot         slot,
		std::string_view    relPath)
	{
		const auto it = m_Materials.find(MaterialKey(material));
		if (it == m_Materials.end())
			throw bgl::SceneError(
				"MaterialHandle passed to SetMaterialTexture is not owned by this AssetManager");

		MaterialRecord& record = it->second;
		if (record.loose)
		{
			throw bgl::SceneError(
				"SetMaterialTexture expects a baked material; use SetMaterialRoute for a loose "
				"one");
		}

		auto path = std::string(relPath);
		switch (slot)
		{
		case TextureSlot::kBaseColor:
			record.source.pbr.baseColorTexture = std::move(path);
			break;
		case TextureSlot::kNormal:
			record.source.pbr.normalTexture = std::move(path);
			break;
		case TextureSlot::kOrm:
			record.source.pbr.ormTexture = std::move(path);
			break;
		}

		RebuildMaterial(record);
	}

	void
	AssetManager::SetMaterialRoute(
		bgl::MaterialHandle material,
		uint32_t            channel,
		std::string_view    relPath,
		uint16_t            sourceChannel)
	{
		const auto it = m_Materials.find(MaterialKey(material));
		if (it == m_Materials.end())
			throw bgl::SceneError(
				"MaterialHandle passed to SetMaterialRoute is not owned by this AssetManager");

		MaterialRecord& record = it->second;
		if (!record.loose)
		{
			throw bgl::SceneError(
				"SetMaterialRoute expects a loose material; use SetMaterialTexture for a baked "
				"one");
		}
		if (channel >= assetlib::c_LooseChannelCount)
			throw bgl::SceneError("channel passed to SetMaterialRoute is out of range");

		record.source.pbr.routes[channel].texture = std::string(relPath);
		record.source.pbr.routes[channel].channel = sourceChannel;

		RebuildMaterial(record);
	}

	void
	AssetManager::RebuildMaterial(MaterialRecord& record)
	{
		const std::vector<std::string> paths = MaterialTextures(record.source, record.loose);

		// Acquire the new set before releasing the old: a texture that survives the swap -- the two
		// maps the edit did not touch, or the same path reassigned -- must not be deleted and
		// re-uploaded, and must keep its bindless slot.
		auto textures = std::vector<bgl::TextureAssetHandle>(paths.size());
		for (size_t i = 0; i < paths.size(); ++i) textures[i] = AcquireTexture(paths[i]);

		const std::vector<bgl::TextureAssetHandle> previous = std::move(record.textures);
		record.textures                                     = std::move(textures);

		// Rewritten in place, so the handle stays valid and every submesh bound to this material
		// follows the change without being rebound.
		if (record.loose)
			m_Scene->UpdateLoosePbrMaterial(record.handle, LooseDesc(record));
		else
			m_Scene->UpdatePbrMaterial(record.handle, BakedDesc(record));

		for (const bgl::TextureAssetHandle texture : previous) ReleaseTexture(texture);
	}

	bgl::PbrMaterialDesc
	AssetManager::BakedDesc(const MaterialRecord& record) const
	{
		const assetlib::PbrParams& pbr = record.source.pbr;

		auto desc               = bgl::PbrMaterialDesc();
		desc.baseColorFactor    = pbr.baseColorFactor;
		desc.metallicFactor     = pbr.metallicFactor;
		desc.roughnessFactor    = pbr.roughnessFactor;
		desc.layerType          = ToLayerType(pbr.alphaMode, m_Options.hashedAsBlend);
		desc.alphaCutoff        = pbr.alphaCutoff;
		desc.transmissionFactor = pbr.transmissionFactor;

		desc.baseColorTexture = record.textures[0];
		desc.normalTexture    = record.textures[1];
		desc.ormTexture       = record.textures[2];

		return desc;
	}

	bgl::LoosePbrMaterialDesc
	AssetManager::LooseDesc(const MaterialRecord& record) const
	{
		const assetlib::PbrParams& pbr = record.source.pbr;

		auto desc               = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor    = pbr.baseColorFactor;
		desc.metallicFactor     = pbr.metallicFactor;
		desc.roughnessFactor    = pbr.roughnessFactor;
		desc.layerType          = ToLayerType(pbr.alphaMode, m_Options.hashedAsBlend);
		desc.alphaCutoff        = pbr.alphaCutoff;
		desc.transmissionFactor = pbr.transmissionFactor;

		const auto route = [&](size_t index) {
			auto out    = bgl::ChannelRouteDesc();
			out.texture = record.textures[index];
			out.channel = pbr.routes[index].channel;
			return out;
		};

		// The runs come from BMaterial.h, which owns `routes` -- never a literal offset.
		for (size_t i = 0; i < desc.baseColor.size(); ++i)
			desc.baseColor[i] = route(assetlib::channelIndex(assetlib::c_BaseColorChannels, i));
		for (size_t i = 0; i < desc.orm.size(); ++i)
			desc.orm[i] = route(assetlib::channelIndex(assetlib::c_OrmChannels, i));
		for (size_t i = 0; i < desc.normal.size(); ++i)
			desc.normal[i] = route(assetlib::channelIndex(assetlib::c_NormalChannels, i));

		return desc;
	}

	// --- Introspection ----------------------------------------------------------------------------

	uint32_t
	AssetManager::TextureRefCount(bgl::TextureAssetHandle texture) const noexcept
	{
		const auto it = m_Textures.find(texture.textureSlot.index);
		return it == m_Textures.end() ? 0 : it->second.refCount;
	}

	uint32_t
	AssetManager::MaterialRefCount(bgl::MaterialHandle material) const noexcept
	{
		const auto it = m_Materials.find(MaterialKey(material));
		return it == m_Materials.end() ? 0 : it->second.refCount;
	}

	uint32_t
	AssetManager::GeomRefCount(bgl::GeomHandle geom) const noexcept
	{
		const auto it = m_Geoms.find(geom.handle.index);
		return it == m_Geoms.end() ? 0 : it->second.refCount;
	}
}
