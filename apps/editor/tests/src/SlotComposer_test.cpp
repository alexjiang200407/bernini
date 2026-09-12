#include "Windows/MaterialEditor/SlotComposer.h"

#include "util/QtSupport.h"  // IWYU pragma: keep

#include <assetlib/image_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <core/containers/fixed_buffer.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <qobject.h>
#include <qstring.h>
#include <qstringliteral.h>
#include <utility>

// The worker half of ADR-8's editor side: a compose runs off the UI thread and its delivery --
// Composed, on the UI thread -- echoes the addressing it was queued under, image or null alike,
// so the window can match it against a board that may have moved on.

namespace
{
	// A scratch directory that cleans up after itself.
	struct ComposeDir
	{
		std::filesystem::path path;

		ComposeDir() : path(std::filesystem::temp_directory_path() / "bernini_slot_composer")
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}
		~ComposeDir() { std::filesystem::remove_all(path); }
	};

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2 whose every texel is `rgba`.
	void
	WriteSource(const std::filesystem::path& file, uint32_t size, std::array<uint8_t, 4> rgba)
	{
		auto image     = assetlib::ImageData();
		image.width    = size;
		image.height   = size;
		image.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
		image.pixels   = core::fixed_buffer<std::byte>(static_cast<size_t>(size) * size * 4);
		for (size_t t = 0; t < static_cast<size_t>(size) * size; ++t)
			for (size_t c = 0; c < 4; ++c)
				image.pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);
		image.subresources = {
			{ 0, static_cast<uint64_t>(size) * 4, static_cast<uint64_t>(size) * size * 4 }
		};

		assetlib::writeKTX2(image, file, false, assetlib::Ktx2Compression::kNone);
	}

	// The angelica shape: AO from ao.ktx2's R, roughness and metallic from mr.ktx2's G and B.
	assetlib::BMaterial
	RoutedMaterial()
	{
		auto mat         = assetlib::BMaterial();
		mat.shadingModel = assetlib::ShadingModel::kPbrSurface;
		mat.surface.name = "Rim";

		auto& orm     = mat.surface.textures.emplace_back();
		orm.name      = "orm";
		orm.routes[0] = { "ao.ktx2", 0 };
		orm.routes[1] = { "mr.ktx2", 1 };
		orm.routes[2] = { "mr.ktx2", 2 };
		return mat;
	}

	struct Delivery
	{
		int                                  graphIndex = -1;
		size_t                               slot       = 0;
		QString                              key;
		std::shared_ptr<assetlib::ImageData> image;
	};

	std::optional<Delivery>
	ComposeAndWait(SlotComposer& composer, assetlib::BMaterial material, const ComposeDir& dir)
	{
		auto delivered = std::optional<Delivery>();
		QObject::connect(
			&composer,
			&SlotComposer::Composed,
			[&](int                                  graphIndex,
		        size_t                               slot,
		        const QString&                       key,
		        std::shared_ptr<assetlib::ImageData> image) {
				delivered = Delivery{ graphIndex, slot, key, std::move(image) };
			});

		composer.Compose(3, 2, QStringLiteral("routes"), std::move(material), "orm", dir.path);

		if (!editor::test::WaitFor([&] { return delivered.has_value(); }))
			return std::nullopt;
		return delivered;
	}
}

TEST_CASE(
	"A queued compose lands on the UI thread with its addressing intact",
	"[materialgraph][surfacesink]")
{
	const ComposeDir dir;
	WriteSource(dir.path / "ao.ktx2", 16, { { 200, 7, 9, 255 } });
	WriteSource(dir.path / "mr.ktx2", 8, { { 3, 60, 90, 255 } });

	SlotComposer composer;

	const std::optional<Delivery> delivered = ComposeAndWait(composer, RoutedMaterial(), dir);
	REQUIRE(delivered.has_value());

	CHECK(delivered->graphIndex == 3);
	CHECK(delivered->slot == 2);
	CHECK(delivered->key == QStringLiteral("routes"));

	// Sized to the largest source -- the compositor's own rule, pinned in assetlib; here it
	// proves the compose really ran rather than echoed.
	REQUIRE(delivered->image != nullptr);
	CHECK(delivered->image->width == 16);
	CHECK(delivered->image->height == 16);
}

TEST_CASE(
	"A compose that cannot read a source delivers null, addressing intact",
	"[materialgraph][surfacesink]")
{
	const ComposeDir dir;  // no sources written

	SlotComposer composer;

	const std::optional<Delivery> delivered = ComposeAndWait(composer, RoutedMaterial(), dir);
	REQUIRE(delivered.has_value());

	// The failure still delivers: the null is what keeps the cache entry from being retried per
	// keystroke, and the key is what lets the window drop it against a board that rerouted.
	CHECK(delivered->image == nullptr);
	CHECK(delivered->key == QStringLiteral("routes"));
}
