#include "util/mime_files.h"

#include <QMimeData>
#include <QUrl>

TEST_CASE("The drop filter takes the first local file with the suffix", "[mime]")
{
	auto mime = QMimeData();
	mime.setUrls(
		{ QUrl("https://example.com/remote.bmesh"),
	      QUrl::fromLocalFile("/tmp/texture.ktx2"),
	      QUrl::fromLocalFile("/tmp/Unit.BMESH"),
	      QUrl::fromLocalFile("/tmp/other.bmesh") });

	CHECK(editor::FirstLocalFileWithSuffix(&mime, u".bmesh") == "/tmp/Unit.BMESH");
	CHECK(editor::FirstLocalFileWithSuffix(&mime, u".benv").isEmpty());
	CHECK(editor::FirstLocalFileWithSuffix(nullptr, u".bmesh").isEmpty());

	auto empty = QMimeData();
	CHECK(editor::FirstLocalFileWithSuffix(&empty, u".bmesh").isEmpty());
}
