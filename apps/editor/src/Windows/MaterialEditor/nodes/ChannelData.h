#pragma once

#include <QtNodes/NodeData>

#include <bgl/TextureAssetHandle.h>

class ChannelData : public QtNodes::NodeData
{
public:
	static constexpr unsigned int c_MaxChannels = 4;

	struct Route
	{
		bgl::TextureAssetHandle texture;
		QString                 path;
		uint16_t                channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};

	ChannelData() = default;

	[[nodiscard]] static ChannelData
	Scalar(bgl::TextureAssetHandle texture, QString path, uint16_t channel)
	{
		auto data        = ChannelData();
		data.m_Count     = 1;
		data.m_Routes[0] = { texture, std::move(path), channel };
		return data;
	}

	[[nodiscard]] static ChannelData
	Bundle(bgl::TextureAssetHandle texture, const QString& path, unsigned int count)
	{
		auto data    = ChannelData();
		data.m_Count = std::min(count, c_MaxChannels);
		for (unsigned int i = 0; i < data.m_Count; ++i)
			data.m_Routes[i] = { texture, path, static_cast<uint16_t>(i) };
		return data;
	}

	[[nodiscard]] static QtNodes::NodeDataType
	Type(unsigned int arity)
	{
		static const char* const c_Names[c_MaxChannels + 1] = { "-", "R", "RG", "RGB", "RGBA" };
		const unsigned int       clamped                    = std::min(arity, c_MaxChannels);

		return QtNodes::NodeDataType{ QStringLiteral("channel%1").arg(clamped),
			                          QString::fromLatin1(c_Names[clamped]) };
	}

	QtNodes::NodeDataType
	type() const override
	{
		return Type(m_Count);
	}

	[[nodiscard]] unsigned int
	Count() const noexcept
	{
		return m_Count;
	}

	[[nodiscard]] Route
	At(unsigned int index) const noexcept
	{
		return index < m_Count ? m_Routes[index] : Route{};
	}

private:
	std::array<Route, c_MaxChannels> m_Routes;
	unsigned int                     m_Count = 0;
};
