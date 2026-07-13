#include "Windows/MaterialEditor/nodes/ChannelData.h"

#include "util/QtSupport.h"

namespace
{
	/**
	 * A texture handle a test can hand around. It never reaches the GPU here -- ChannelData only
	 * carries it -- so an invalid one is all these tests need.
	 */
	bgl::TextureAssetHandle
	AnyTexture()
	{
		return bgl::TextureAssetHandle{};
	}
}

TEST_CASE("A channel type is named for its arity", "[channeldata]")
{
	struct Row
	{
		unsigned int arity;
		const char*  id;
		const char*  name;
	};

	// The id is what MaterialGraphModel compares to decide whether two ports may be wired together,
	// so this table *is* the connection-compatibility rule.
	const Row row = GENERATE(
		Row{ 0, "channel0", "-" },
		Row{ 1, "channel1", "R" },
		Row{ 2, "channel2", "RG" },
		Row{ 3, "channel3", "RGB" },
		Row{ 4, "channel4", "RGBA" });

	INFO("arity " << row.arity);

	const QtNodes::NodeDataType type = ChannelData::Type(row.arity);

	REQUIRE(type.id == QString(row.id));
	REQUIRE(type.name == QString(row.name));
}

TEST_CASE("An arity past RGBA is clamped", "[channeldata]")
{
	// There is no fifth channel to name, and indexing the name table for one would run off it.
	REQUIRE(ChannelData::Type(5).id == QString("channel4"));
	REQUIRE(ChannelData::Type(99).id == QString("channel4"));
}

TEST_CASE("An empty ChannelData routes nothing", "[channeldata]")
{
	const ChannelData data;

	REQUIRE(data.Count() == 0u);
	REQUIRE(data.type().id == QString("channel0"));
	REQUIRE(data.At(0).path.isEmpty());
}

TEST_CASE("A scalar carries one channel", "[channeldata]")
{
	const ChannelData data = ChannelData::Scalar(AnyTexture(), "Textures/rough.ktx2", 2);

	REQUIRE(data.Count() == 1u);
	REQUIRE(data.At(0).path == QString("Textures/rough.ktx2"));

	// A scalar names the channel it was cut from, which is not necessarily channel 0 -- that is how
	// roughness reaches the output from an ORM texture's green.
	REQUIRE(data.At(0).channel == 2);
}

TEST_CASE("A bundle numbers its channels in order", "[channeldata]")
{
	const ChannelData data = ChannelData::Bundle(AnyTexture(), "Textures/albedo.ktx2", 3);

	REQUIRE(data.Count() == 3u);
	for (unsigned int i = 0; i < 3; ++i)
	{
		INFO("channel " << i);
		REQUIRE(data.At(i).channel == i);
		REQUIRE(data.At(i).path == QString("Textures/albedo.ktx2"));
	}
}

TEST_CASE("A bundle is clamped to RGBA", "[channeldata]")
{
	const ChannelData data = ChannelData::Bundle(AnyTexture(), "Textures/albedo.ktx2", 9);

	REQUIRE(data.Count() == ChannelData::c_MaxChannels);
}

TEST_CASE("A bundle of nothing routes nothing", "[channeldata]")
{
	const ChannelData data = ChannelData::Bundle(AnyTexture(), "Textures/albedo.ktx2", 0);

	REQUIRE(data.Count() == 0u);
	REQUIRE(data.At(0).path.isEmpty());
}

TEST_CASE("Reading past the last channel gives nothing", "[channeldata]")
{
	const ChannelData data = ChannelData::Scalar(AnyTexture(), "Textures/rough.ktx2", 1);

	// A default Route, not the stale contents of the backing array.
	REQUIRE(data.At(1).path.isEmpty());
	REQUIRE(data.At(1).channel == 0);
	REQUIRE(data.At(ChannelData::c_MaxChannels).path.isEmpty());
}

TEST_CASE("A ChannelData's type follows how many channels it holds", "[channeldata]")
{
	REQUIRE(ChannelData::Scalar(AnyTexture(), "a.ktx2", 0).type().id == QString("channel1"));
	REQUIRE(ChannelData::Bundle(AnyTexture(), "a.ktx2", 2).type().id == QString("channel2"));
	REQUIRE(ChannelData::Bundle(AnyTexture(), "a.ktx2", 4).type().id == QString("channel4"));
}
