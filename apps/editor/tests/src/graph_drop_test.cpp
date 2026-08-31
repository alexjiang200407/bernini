#include "Windows/MaterialEditor/graph_drop.h"

#include <QMimeData>
#include <QUrl>

namespace
{
	/** A drag carrying `paths` as local files, in order. */
	void
	SetLocalFiles(QMimeData& mime, const QStringList& paths)
	{
		auto urls = QList<QUrl>();
		for (const QString& path : paths) urls.push_back(QUrl::fromLocalFile(path));

		mime.setUrls(urls);
	}
}

TEST_CASE("The graph canvas takes a texture or a source", "[materialgraph][drop]")
{
	auto mime = QMimeData();

	SetLocalFiles(mime, { "/tmp/albedo.ktx2" });
	CHECK(editor::IsGraphDrag(&mime));

	SetLocalFiles(mime, { "/tmp/kirk.glb" });
	CHECK(editor::IsGraphDrag(&mime));

	SetLocalFiles(mime, { "/tmp/ALBEDO.KTX2" });
	CHECK(editor::IsGraphDrag(&mime));

	SECTION("and lets everything else through to the view beneath")
	{
		// A `.bmesh` is a mesh viewport's drop, and the canvas must not swallow it -- the base
		// class is what handles a drag the canvas has no answer for.
		SetLocalFiles(mime, { "/tmp/kirk.bmesh", "/tmp/skin.bmaterial", "/tmp/day.benv" });
		CHECK_FALSE(editor::IsGraphDrag(&mime));

		SetLocalFiles(mime, { "/tmp/albedo.png" });
		CHECK_FALSE(editor::IsGraphDrag(&mime));

		auto empty = QMimeData();
		CHECK_FALSE(editor::IsGraphDrag(&empty));
		CHECK_FALSE(editor::IsGraphDrag(nullptr));
	}

	SECTION("a remote URL is not a file")
	{
		mime.setUrls({ QUrl("https://example.com/albedo.ktx2") });
		CHECK_FALSE(editor::IsGraphDrag(&mime));
	}
}

TEST_CASE("A dropped texture is a node, a dropped source is a question", "[materialgraph][drop]")
{
	auto mime = QMimeData();

	SetLocalFiles(mime, { "/tmp/albedo.ktx2" });
	const editor::GraphDrop texture = editor::GraphDroppedOn(&mime);
	CHECK(texture.texture == QString("/tmp/albedo.ktx2"));
	CHECK(texture.source.isEmpty());

	SetLocalFiles(mime, { "/tmp/kirk.glb" });
	const editor::GraphDrop source = editor::GraphDroppedOn(&mime);
	CHECK(source.source == QString("/tmp/kirk.glb"));
	CHECK(source.texture.isEmpty());

	SECTION("and a texture dragged with a source wins, needing nothing asked")
	{
		SetLocalFiles(mime, { "/tmp/kirk.glb", "/tmp/albedo.ktx2" });

		const editor::GraphDrop both = editor::GraphDroppedOn(&mime);
		CHECK(both.texture == QString("/tmp/albedo.ktx2"));
		CHECK(both.source.isEmpty());
	}

	SECTION("and a drag of neither offers nothing")
	{
		SetLocalFiles(mime, { "/tmp/kirk.bmesh" });

		const editor::GraphDrop none = editor::GraphDroppedOn(&mime);
		CHECK(none.texture.isEmpty());
		CHECK(none.source.isEmpty());
	}
}
