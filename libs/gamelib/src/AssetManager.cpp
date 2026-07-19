#include <gamelib/AssetManager.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/image_io.h>

namespace game
{
	namespace
	{
		// The asset says what the material *is* (its glTF-shaped alpha mode); the renderer says which
		// pass composites it. They are separate enums because assetlib cannot link bgl, and separate
		// concepts because a layer is free to grow buckets no material ever authors. gamelib is the
		// only library that links both, so this is the one place they meet.
		bgl::LayerType
		ToLayerType(assetlib::AlphaMode mode)
		{
			switch (mode)
			{
			case assetlib::AlphaMode::kMask:
				return bgl::LayerType::kMask;
			case assetlib::AlphaMode::kBlend:
				return bgl::LayerType::kBlend;
			case assetlib::AlphaMode::kOpaque:
				break;
			}
			return bgl::LayerType::kOpaque;
		}

	}

	// The order MaterialRecord::textures parallels: the baked triplet, or the nine authoring routes.
	// One order per mode, in one place, so the record's texture references and the desc it rebuilds
	// can never fall out of step.
	std::vector<std::string>
	materialTextures(const assetlib::BMaterial& material)
	{
		const assetlib::PbrParams& pbr = material.pbr;

		if (material.mode == assetlib::MaterialMode::kLoose)
		{
			auto paths = std::vector<std::string>(assetlib::c_LooseChannelCount);
			for (size_t i = 0; i < assetlib::c_LooseChannelCount; ++i)
				paths[i] = pbr.routes[i].texture;
			return paths;
		}

		return { pbr.baseColorTexture, pbr.normalTexture, pbr.ormTexture };
	}

	AssetManager::AssetManager(bgl::SceneRef scene, std::filesystem::path dataRoot) :
		m_Scene(std::move(scene)), m_DataRoot(std::move(dataRoot))
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
		auto decoded = assetlib::ImageData();
		bool hoisted = false;
		if (prefetch != nullptr)
		{
			if (const auto it = prefetch->find(relPath); it != prefetch->end())
			{
				decoded = std::move(it->second);
				prefetch->erase(it);
				hoisted = true;
			}
		}

		auto key = std::string(relPath);

		const bgl::TextureAssetHandle handle = m_Scene->AddTextureAsset(
			hoisted ? std::move(decoded) : assetlib::loadKTX2(m_DataRoot / key),
			key);

		m_TextureByPath.emplace(key, handle.textureSlot.index);
		m_Textures.emplace(handle.textureSlot.index, TextureRecord{ std::move(key), handle, 1 });

		return handle;
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
		// unspecified, so passing `loadMaterial(m_DataRoot / key)` alongside `std::move(key)` lets the
		// compiler move `key` out from under the path it is supposed to build.
		const assetlib::BMaterial material = assetlib::loadMaterial(m_DataRoot / key);

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

		// Acquire the textures first: the desc the scene needs is built out of their handles.
		const std::vector<std::string> paths = materialTextures(material);

		auto textures = std::vector<bgl::TextureAssetHandle>(paths.size());
		for (size_t i = 0; i < paths.size(); ++i) textures[i] = AcquireTexture(paths[i], prefetch);

		auto record     = MaterialRecord();
		record.key      = key;
		record.source   = material;
		record.textures = std::move(textures);
		record.refCount = 1;

		record.handle = material.mode == assetlib::MaterialMode::kLoose ?
		                    m_Scene->CreateLoosePbrMaterial(LooseDesc(record)) :
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

		const assetlib::BMesh mesh = assetlib::load(m_DataRoot / relPath);

		if (meshIndex >= mesh.meshes.size())
		{
			throw std::runtime_error(
				std::format(
					"AssetManager: mesh index {} out of range in '{}'",
					meshIndex,
					relPath));
		}

		const assetlib::Mesh& entry = mesh.meshes[meshIndex];

		// AddStaticMesh wants a list parallel to the .bmesh's own material list, but only the
		// materials *this* mesh's submeshes name are worth loading -- another mesh in the same file
		// may name others. Anything left invalid renders unlit, which is what AddStaticMesh does with
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
		record.handle           = m_Scene->AddStaticMesh(mesh, meshIndex, materials);
		record.key              = key;
		record.submeshMaterials = std::move(submeshMaterials);
		record.refCount         = 1;

		const uint32_t slot = record.handle.handle.index;

		m_GeomByPath.emplace(key, slot);

		const bgl::GeomHandle handle = record.handle;
		m_Geoms.emplace(slot, std::move(record));

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

		// The instance's reference is what keeps the geometry alive while it is being drawn.
		++it->second.refCount;

		const InstanceKey key{ view.Get(), instance.handle.index };

		auto record      = InstanceRecord();
		record.handle    = instance;
		record.view      = std::move(view);
		record.geomSlot  = geom.handle.index;
		record.overrides = std::vector<bgl::MaterialHandle>(it->second.submeshMaterials.size());

		m_Instances.emplace(key, std::move(record));

		return instance;
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
		if (record.source.mode != assetlib::MaterialMode::kBaked)
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
		if (record.source.mode != assetlib::MaterialMode::kLoose)
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
		const std::vector<std::string> paths = materialTextures(record.source);

		// Acquire the new set before releasing the old: a texture that survives the swap -- the two
		// maps the edit did not touch, or the same path reassigned -- must not be deleted and
		// re-uploaded, and must keep its bindless slot.
		auto textures = std::vector<bgl::TextureAssetHandle>(paths.size());
		for (size_t i = 0; i < paths.size(); ++i) textures[i] = AcquireTexture(paths[i]);

		const std::vector<bgl::TextureAssetHandle> previous = std::move(record.textures);
		record.textures                                     = std::move(textures);

		// Rewritten in place, so the handle stays valid and every submesh bound to this material
		// follows the change without being rebound.
		if (record.source.mode == assetlib::MaterialMode::kLoose)
			m_Scene->UpdateLoosePbrMaterial(record.handle, LooseDesc(record));
		else
			m_Scene->UpdatePbrMaterial(record.handle, BakedDesc(record));

		for (const bgl::TextureAssetHandle texture : previous) ReleaseTexture(texture);
	}

	bgl::PbrMaterialDesc
	AssetManager::BakedDesc(const MaterialRecord& record) const
	{
		const assetlib::PbrParams& pbr = record.source.pbr;

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = pbr.baseColorFactor;
		desc.metallicFactor  = pbr.metallicFactor;
		desc.roughnessFactor = pbr.roughnessFactor;
		desc.layerType       = ToLayerType(pbr.alphaMode);
		desc.alphaCutoff     = pbr.alphaCutoff;
		desc.occlude         = pbr.occlude;

		desc.baseColorTexture = record.textures[0];
		desc.normalTexture    = record.textures[1];
		desc.ormTexture       = record.textures[2];

		return desc;
	}

	bgl::LoosePbrMaterialDesc
	AssetManager::LooseDesc(const MaterialRecord& record) const
	{
		const assetlib::PbrParams& pbr = record.source.pbr;

		auto desc            = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor = pbr.baseColorFactor;
		desc.metallicFactor  = pbr.metallicFactor;
		desc.roughnessFactor = pbr.roughnessFactor;
		desc.layerType       = ToLayerType(pbr.alphaMode);
		desc.alphaCutoff     = pbr.alphaCutoff;
		desc.occlude         = pbr.occlude;

		const auto route = [&](size_t index) {
			auto out    = bgl::ChannelRouteDesc();
			out.texture = record.textures[index];
			out.channel = pbr.routes[index].channel;
			return out;
		};

		// The runs come from BMaterial.h, which owns `routes` -- never a literal offset.
		for (size_t i = 0; i < desc.baseColor.size(); ++i)
			desc.baseColor[i] = route(assetlib::ChannelIndex(assetlib::c_BaseColorChannels, i));
		for (size_t i = 0; i < desc.orm.size(); ++i)
			desc.orm[i] = route(assetlib::ChannelIndex(assetlib::c_OrmChannels, i));
		for (size_t i = 0; i < desc.normal.size(); ++i)
			desc.normal[i] = route(assetlib::ChannelIndex(assetlib::c_NormalChannels, i));

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
