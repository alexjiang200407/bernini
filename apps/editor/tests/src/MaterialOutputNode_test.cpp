#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

#include "util/QtSupport.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>

namespace
{
	using QtNodes::PortType;

	/** Canonical channel indices, in the order MaterialOutputNode::Route takes them. */
	enum Channel : unsigned int
	{
		kBaseR = 0,
		kBaseG,
		kBaseB,
		kBaseA,
		kAO,
		kRoughness,
		kMetallic,
		kNormalX,
		kNormalY,
	};

	/** The three collapsed ports of a freshly built node: base colour, ORM, normal. */
	enum Group : QtNodes::PortIndex
	{
		kBaseColorPort = 0,
		kOrmPort,
		kNormalPort,
	};

	std::shared_ptr<ChannelData>
	Bundle(const QString& path, unsigned int count)
	{
		return std::make_shared<ChannelData>(
			ChannelData::Bundle(bgl::TextureAssetHandle{}, path, count));
	}

	std::shared_ptr<ChannelData>
	Scalar(const QString& path, uint16_t channel)
	{
		return std::make_shared<ChannelData>(
			ChannelData::Scalar(bgl::TextureAssetHandle{}, path, channel));
	}

	/** The state a node saves and loads, with the groups named rather than positional. */
	QJsonObject
	State(bool splitBaseColor, bool splitOrm, bool splitNormal)
	{
		QJsonObject json;
		json["split"] = QJsonArray{ splitBaseColor, splitOrm, splitNormal };
		return json;
	}
}

TEST_CASE("A material output starts with one port per group", "[materialoutput]")
{
	const MaterialOutputNode node;

	// A group of more than one channel shows one wide port until it is split, so three groups are
	// three ports -- not the nine channels behind them.
	REQUIRE(node.nPorts(PortType::In) == 3u);

	SECTION("and each collapsed port takes its whole group's width")
	{
		// Opaque base colour is RGB, not RGBA: alpha is not exposed, so a texture's RGBA bundle will
		// not connect to it. That refusal is the type id's doing.
		REQUIRE(node.dataType(PortType::In, kBaseColorPort).id == QString("channel3"));
		REQUIRE(node.dataType(PortType::In, kOrmPort).id == QString("channel3"));
		REQUIRE(node.dataType(PortType::In, kNormalPort).id == QString("channel2"));
	}

	SECTION("and each is captioned with its group")
	{
		REQUIRE(node.portCaption(PortType::In, kBaseColorPort) == QString("Base Color"));
		REQUIRE(node.portCaption(PortType::In, kOrmPort) == QString("ORM"));
		REQUIRE(node.portCaption(PortType::In, kNormalPort) == QString("Normal"));
		REQUIRE(node.portCaption(PortType::In, 3).isEmpty());
	}

	SECTION("and nothing is routed until something is wired")
	{
		for (unsigned int channel = 0; channel < MaterialOutputNode::c_ChannelCount; ++channel)
		{
			INFO("channel " << channel);
			REQUIRE(node.Route(channel).path.isEmpty());
		}
	}
}

TEST_CASE("A material output is the sink", "[materialoutput]")
{
	MaterialOutputNode node;

	// Everything flows into it and nothing out.
	REQUIRE(node.nPorts(PortType::Out) == 0u);
	REQUIRE(node.outData(0) == nullptr);
}

TEST_CASE("A bundle wired into a group routes every channel of it", "[materialoutput]")
{
	MaterialOutputNode node;

	node.setInData(Bundle("Textures/albedo.ktx2", 3), kBaseColorPort);
	node.setInData(Bundle("Textures/orm.ktx2", 3), kOrmPort);
	node.setInData(Bundle("Textures/normal.ktx2", 2), kNormalPort);

	// One wire per group, but every channel behind it comes through -- each taking its colour from
	// its own position in the bundle.
	REQUIRE(node.Route(kBaseR).path == QString("Textures/albedo.ktx2"));
	REQUIRE(node.Route(kBaseR).channel == 0);
	REQUIRE(node.Route(kBaseB).channel == 2);

	REQUIRE(node.Route(kAO).path == QString("Textures/orm.ktx2"));
	REQUIRE(node.Route(kAO).channel == 0);
	REQUIRE(node.Route(kRoughness).channel == 1);
	REQUIRE(node.Route(kMetallic).channel == 2);

	REQUIRE(node.Route(kNormalX).path == QString("Textures/normal.ktx2"));
	REQUIRE(node.Route(kNormalY).channel == 1);
}

TEST_CASE("An opaque base colour never routes alpha", "[materialoutput]")
{
	MaterialOutputNode node;

	// Even given a full RGBA bundle, an opaque material has nowhere to put the alpha: it exposes
	// three base-colour channels, so the fourth is not a channel it has.
	node.setInData(Bundle("Textures/albedo.ktx2", 4), kBaseColorPort);

	REQUIRE(node.Route(kBaseB).path == QString("Textures/albedo.ktx2"));
	REQUIRE(node.Route(kBaseA).path.isEmpty());
}

TEST_CASE("Unwiring a port clears its routes", "[materialoutput]")
{
	MaterialOutputNode node;

	node.setInData(Bundle("Textures/albedo.ktx2", 3), kBaseColorPort);
	REQUIRE(!node.Route(kBaseR).path.isEmpty());

	// QtNodes pushes a null payload when a wire is pulled out, and that has to actually unroute the
	// channels rather than leave the last texture stuck to them.
	node.setInData(nullptr, kBaseColorPort);

	REQUIRE(node.Route(kBaseR).path.isEmpty());
}

TEST_CASE("Routing past the last channel gives nothing", "[materialoutput]")
{
	MaterialOutputNode node;

	node.setInData(Bundle("Textures/albedo.ktx2", 3), kBaseColorPort);

	REQUIRE(node.Route(MaterialOutputNode::c_ChannelCount).path.isEmpty());
	REQUIRE(node.Route(999).path.isEmpty());
}

TEST_CASE("Wiring a port announces a change", "[materialoutput]")
{
	MaterialOutputNode node;
	QSignalSpy         changed(&node, &MaterialOutputNode::Changed);

	node.setInData(Bundle("Textures/albedo.ktx2", 3), kBaseColorPort);
	REQUIRE(changed.count() == 1);

	// Including when it is unwired: the material has changed either way, and the preview has to hear
	// about it.
	node.setInData(nullptr, kBaseColorPort);
	REQUIRE(changed.count() == 2);
}

TEST_CASE("Splitting a group gives it a port per channel", "[materialoutput]")
{
	MaterialOutputNode node;

	SECTION("base colour splits into three, and the rest stay collapsed")
	{
		node.load(State(true, false, false));

		REQUIRE(node.nPorts(PortType::In) == 5u);
	}

	SECTION("a split group's ports are one channel wide")
	{
		node.load(State(false, true, false));

		// Base colour is still one wide RGB port; ORM is now three scalar ports; normal follows.
		REQUIRE(node.dataType(PortType::In, 0).id == QString("channel3"));
		REQUIRE(node.dataType(PortType::In, 1).id == QString("channel1"));
		REQUIRE(node.dataType(PortType::In, 2).id == QString("channel1"));
		REQUIRE(node.dataType(PortType::In, 3).id == QString("channel1"));
		REQUIRE(node.dataType(PortType::In, 4).id == QString("channel2"));
	}

	SECTION("a split group's ports are captioned per channel")
	{
		node.load(State(true, true, true));

		// 3 base + 3 ORM + 2 normal. No opaque base alpha.
		REQUIRE(node.nPorts(PortType::In) == 8u);

		const QStringList expected = { "Base R",    "Base G",   "Base B",   "AO",
			                           "Roughness", "Metallic", "Normal X", "Normal Y" };

		for (int port = 0; port < expected.size(); ++port)
		{
			INFO("port " << port);
			REQUIRE(node.portCaption(PortType::In, port) == expected[port]);
		}
	}
}

TEST_CASE("A scalar wired into a split port routes only that channel", "[materialoutput]")
{
	MaterialOutputNode node;

	node.load(State(false, true, false));

	// Roughness is ORM's second port (ports 1, 2, 3 are AO, Roughness, Metallic), and it is being fed
	// the green channel of a texture that is not an ORM map at all. That is the whole point of
	// splitting a group.
	node.setInData(Scalar("Textures/rough.ktx2", 1), 2);

	REQUIRE(node.Route(kRoughness).path == QString("Textures/rough.ktx2"));
	REQUIRE(node.Route(kRoughness).channel == 1);

	REQUIRE(node.Route(kAO).path.isEmpty());
	REQUIRE(node.Route(kMetallic).path.isEmpty());
}

TEST_CASE("A material output's factors default to one", "[materialoutput]")
{
	const MaterialOutputNode node;

	REQUIRE(node.BaseColorFactor() == glm::vec4(1.0f));
	REQUIRE(node.MetallicFactor() == 1.0f);
	REQUIRE(node.RoughnessFactor() == 1.0f);
	REQUIRE(!node.IsAlphaTested());
	REQUIRE(node.name() == QString("MaterialOutput"));
}

TEST_CASE("A material output round-trips its factors and its splits", "[materialoutput]")
{
	MaterialOutputNode saved;

	QJsonObject authored   = State(true, false, true);
	authored["baseColorR"] = 0.25;
	authored["baseColorG"] = 0.5;
	authored["baseColorB"] = 0.75;
	authored["baseColorA"] = 0.5;
	authored["metallic"]   = 0.125;
	authored["roughness"]  = 0.875;
	saved.load(authored);

	MaterialOutputNode reloaded;
	reloaded.load(saved.save());

	REQUIRE(reloaded.BaseColorFactor() == glm::vec4(0.25f, 0.5f, 0.75f, 0.5f));
	REQUIRE(reloaded.MetallicFactor() == 0.125f);
	REQUIRE(reloaded.RoughnessFactor() == 0.875f);

	// The split state has to survive too, or a reloaded graph would have fewer ports than the
	// connections saved alongside it refer to. 3 base + 1 ORM + 2 normal.
	REQUIRE(reloaded.nPorts(PortType::In) == saved.nPorts(PortType::In));
	REQUIRE(reloaded.nPorts(PortType::In) == 6u);
}

TEST_CASE("A missing factor loads as one", "[materialoutput]")
{
	MaterialOutputNode node;
	QSignalSpy         changed(&node, &MaterialOutputNode::Changed);

	// A graph written before a factor existed still has to load, and an absent factor means "leave
	// the channel alone" -- which is 1.0, not 0.0.
	node.load(QJsonObject{});

	REQUIRE(node.BaseColorFactor() == glm::vec4(1.0f));
	REQUIRE(node.MetallicFactor() == 1.0f);
	REQUIRE(node.RoughnessFactor() == 1.0f);
	REQUIRE(node.nPorts(PortType::In) == 3u);

	REQUIRE(changed.count() == 1);
}

TEST_CASE("An alpha tested material output is a cutout", "[materialoutput]")
{
	const AlphaTestedMaterialOutputNode node;

	REQUIRE(node.IsAlphaTested());
	REQUIRE(node.AlphaCutoff() == 0.5f);
	REQUIRE(node.name() == QString("AlphaTestedMaterialOutput"));

	// The counterpart to the opaque node's RGB: a cutout needs the alpha, so its base-colour port is
	// four wide -- and an RGB bundle will not fit it.
	REQUIRE(node.dataType(PortType::In, kBaseColorPort).id == QString("channel4"));
}

TEST_CASE("An alpha tested base colour routes alpha", "[materialoutput]")
{
	AlphaTestedMaterialOutputNode node;

	node.setInData(Bundle("Textures/leaf.ktx2", 4), kBaseColorPort);

	REQUIRE(node.Route(kBaseA).path == QString("Textures/leaf.ktx2"));
	REQUIRE(node.Route(kBaseA).channel == 3);
}

TEST_CASE("An alpha tested material output round-trips its cutoff", "[materialoutput]")
{
	AlphaTestedMaterialOutputNode saved;

	QJsonObject authored;
	authored["alphaCutoff"] = 0.25;
	saved.load(authored);
	REQUIRE(saved.AlphaCutoff() == 0.25f);

	AlphaTestedMaterialOutputNode reloaded;
	reloaded.load(saved.save());

	REQUIRE(reloaded.AlphaCutoff() == 0.25f);
}

TEST_CASE("A missing cutoff loads as the default", "[materialoutput]")
{
	AlphaTestedMaterialOutputNode node;

	node.load(QJsonObject{});

	REQUIRE(node.AlphaCutoff() == 0.5f);
}
