#include "Windows/MaterialEditor/nodes/TextureNode.h"

#include "util/QtSupport.h"

#include <QJsonObject>

namespace
{
	using QtNodes::PortType;

	/**
	 * A TextureNode with neither a scene nor a preview cache -- which the node explicitly supports,
	 * and which is what the editor itself builds when it runs without graphics.
	 */
	std::unique_ptr<TextureNode>
	HeadlessNode()
	{
		return std::make_unique<TextureNode>(nullptr, nullptr);
	}
}

TEST_CASE("A texture node is all outputs", "[texturenode]")
{
	const auto node = HeadlessNode();

	// It is a source: three bundle ports and four scalar ones, and nothing flows in.
	REQUIRE(node->nPorts(PortType::Out) == TextureNode::c_PortCount);
	REQUIRE(node->nPorts(PortType::Out) == 7u);
	REQUIRE(node->nPorts(PortType::In) == 0u);
	REQUIRE(node->name() == QString("Texture"));
}

TEST_CASE("The bundle ports narrow from RGBA", "[texturenode]")
{
	// Ports 0..2 offer the whole texture at successively narrower widths, so one texture can feed a
	// 4-wide cutout base colour, a 3-wide opaque one, or a 2-wide normal map.
	REQUIRE(TextureNode::ArityOf(0) == 4u);
	REQUIRE(TextureNode::ArityOf(1) == 3u);
	REQUIRE(TextureNode::ArityOf(2) == 2u);
}

TEST_CASE("The channel ports are scalar", "[texturenode]")
{
	for (QtNodes::PortIndex port = 3; port < 7; ++port)
	{
		INFO("port " << port);
		REQUIRE(TextureNode::ArityOf(port) == 1u);
	}
}

TEST_CASE("A texture node's port types follow their arity", "[texturenode]")
{
	const auto node = HeadlessNode();

	REQUIRE(node->dataType(PortType::Out, 0).id == QString("channel4"));
	REQUIRE(node->dataType(PortType::Out, 1).id == QString("channel3"));
	REQUIRE(node->dataType(PortType::Out, 2).id == QString("channel2"));
	REQUIRE(node->dataType(PortType::Out, 3).id == QString("channel1"));
	REQUIRE(node->dataType(PortType::Out, 6).id == QString("channel1"));
}

TEST_CASE("A texture node's ports are captioned", "[texturenode]")
{
	struct Row
	{
		QtNodes::PortIndex port;
		const char*        caption;
	};

	const Row row = GENERATE(
		Row{ 0, "RGBA" },
		Row{ 1, "RGB" },
		Row{ 2, "RG" },
		Row{ 3, "R" },
		Row{ 4, "G" },
		Row{ 5, "B" },
		Row{ 6, "A" });

	INFO("port " << row.port);

	REQUIRE(HeadlessNode()->portCaption(PortType::Out, row.port) == QString(row.caption));
}

TEST_CASE("A port past the last has no caption", "[texturenode]")
{
	REQUIRE(HeadlessNode()->portCaption(PortType::Out, TextureNode::c_PortCount).isEmpty());
}

TEST_CASE("A texture node round-trips its texture path", "[texturenode]")
{
	const auto saved = HeadlessNode();
	saved->SetTexturePath("Textures/albedo.ktx2");

	const QJsonObject json = saved->save();
	REQUIRE(json["texture"].toString() == QString("Textures/albedo.ktx2"));

	// The path is what a saved graph carries; the decoded texture behind it is rebuilt on load.
	const auto reloaded = HeadlessNode();
	reloaded->load(json);

	REQUIRE(reloaded->save()["texture"].toString() == QString("Textures/albedo.ktx2"));
}

TEST_CASE("Loading nothing leaves a texture node's path empty", "[texturenode]")
{
	const auto node = HeadlessNode();

	node->load(QJsonObject{});

	REQUIRE(node->save()["texture"].toString().isEmpty());
}

TEST_CASE("A texture node with no path routes nothing", "[texturenode]")
{
	const auto node = HeadlessNode();

	// Nothing is named, so there is nothing to route: every port stays empty.
	for (QtNodes::PortIndex port = 0; port < QtNodes::PortIndex(TextureNode::c_PortCount); ++port)
	{
		INFO("port " << port);
		REQUIRE(node->outData(port) == nullptr);
	}
}

TEST_CASE("A texture node routes its file even with no scene to load it into", "[texturenode]")
{
	const auto node = HeadlessNode();
	node->SetTexturePath("Textures/albedo.ktx2");

	// A route is a (file, channel) pair, and the file is named whether or not anything could load it.
	// The handle is null here -- there is no scene to upload to -- and that is not the same as being
	// unrouted: Scene::BuildLoosePbrMaterial resolves a null handle to the very default an unrouted
	// channel gets, so the material renders identically either way. What differs is what a *save*
	// records, and gating this on the handle would quietly drop the route from the material the graph
	// compiles to -- unwiring a channel because its texture could not be shown.
	for (QtNodes::PortIndex port = 0; port < QtNodes::PortIndex(TextureNode::c_PortCount); ++port)
	{
		INFO("port " << port);

		const auto data = std::dynamic_pointer_cast<ChannelData>(node->outData(port));
		REQUIRE(data != nullptr);
		REQUIRE(data->At(0).path == QString("Textures/albedo.ktx2"));
		REQUIRE_FALSE(data->At(0).texture.textureSlot);
	}
}
