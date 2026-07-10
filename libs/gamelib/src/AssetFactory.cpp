#include <gamelib/AssetFactory.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>

namespace game
{
	namespace
	{
		constexpr size_t c_BaseColorFirst = 0;
		constexpr size_t c_OrmFirst       = 4;
		constexpr size_t c_NormalFirst    = 7;
	}

	AssetFactory::AssetFactory(bgl::IScene& scene, std::filesystem::path dataRoot) :
		m_Scene(scene), m_DataRoot(std::move(dataRoot))
	{}

	bgl::TextureAssetHandle
	AssetFactory::LoadTexture(const std::string& relPath)
	{
		if (relPath.empty())
			return {};

		if (const auto it = m_Textures.find(relPath); it != m_Textures.end())
			return it->second;

		const auto handle =
			m_Scene.AddTextureAsset(assetlib::loadKTX2(m_DataRoot / relPath), relPath);
		m_Textures.emplace(relPath, handle);
		return handle;
	}

	bgl::MaterialHandle
	AssetFactory::LoadMaterial(const std::string& relPath)
	{
		if (const auto it = m_Materials.find(relPath); it != m_Materials.end())
			return it->second;

		const assetlib::BMaterial material = assetlib::loadMaterial(m_DataRoot / relPath);

		const bgl::MaterialHandle handle = CreateMaterial(material);
		m_Materials.emplace(relPath, handle);
		return handle;
	}

	bgl::MaterialHandle
	AssetFactory::CreateMaterial(const assetlib::BMaterial& material)
	{
		if (material.mode == assetlib::MaterialMode::kLoose)
		{
			auto desc            = bgl::LoosePbrMaterialDesc();
			desc.baseColorFactor = material.baseColorFactor;
			desc.metallicFactor  = material.metallicFactor;
			desc.roughnessFactor = material.roughnessFactor;

			const auto route = [&](size_t index) {
				const assetlib::ChannelRoute& source = material.routes[index];

				auto out    = bgl::ChannelRouteDesc();
				out.texture = LoadTexture(source.texture);
				out.channel = source.channel;
				return out;
			};

			for (size_t i = 0; i < desc.baseColor.size(); ++i)
				desc.baseColor[i] = route(c_BaseColorFirst + i);
			for (size_t i = 0; i < desc.orm.size(); ++i) desc.orm[i] = route(c_OrmFirst + i);
			for (size_t i = 0; i < desc.normal.size(); ++i)
				desc.normal[i] = route(c_NormalFirst + i);

			return m_Scene.CreateLoosePbrMaterial(desc);
		}

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = material.baseColorFactor;
		desc.metallicFactor  = material.metallicFactor;
		desc.roughnessFactor = material.roughnessFactor;

		desc.baseColorTexture = LoadTexture(material.baseColorTexture);
		desc.normalTexture    = LoadTexture(material.normalTexture);
		desc.ormTexture       = LoadTexture(material.ormTexture);

		return m_Scene.CreatePbrMaterial(desc);
	}
}
