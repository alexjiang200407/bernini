#pragma once

#include <QtNodes/NodeData>
#include <QtNodes/internal/NodeData.hpp>

#include <bgl/TextureAssetHandle.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <utility>

/**
 * What a surface slot port carries: one whole texture, bound rather than composited.
 *
 * The type id is its own, so a routed channel -- ChannelData, ids `channel1`..`channel4` -- cannot
 * be wired into a surface slot at all. That is the contract's "bound, not composited" rule
 * (docs/game_defined_surfaces.md) enforced by the graph's port types rather than stated beside
 * them.
 */
class SurfaceTextureData : public QtNodes::NodeData
{
public:
	SurfaceTextureData(bgl::TextureAssetHandle texture, QString path) :
		m_Texture(texture), m_Path(std::move(path))
	{}

	[[nodiscard]] static QtNodes::NodeDataType
	Type()
	{
		return QtNodes::NodeDataType{ QStringLiteral("surfacetexture"), QStringLiteral("Texture") };
	}

	QtNodes::NodeDataType
	type() const override
	{
		return Type();
	}

	[[nodiscard]] bgl::TextureAssetHandle
	Texture() const noexcept
	{
		return m_Texture;
	}

	[[nodiscard]] QString
	Path() const
	{
		return m_Path;
	}

private:
	bgl::TextureAssetHandle m_Texture;
	QString                 m_Path;
};
