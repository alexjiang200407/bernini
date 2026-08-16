#include "util/VatSynth.h"

namespace bgl::test::vat_synth
{
	assetlib::ImageData
	MakeImage(uint32_t columns, uint32_t rows, assetlib::VkFormat format, uint32_t texelBytes)
	{
		auto image      = assetlib::ImageData();
		image.width     = columns;
		image.height    = rows;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = format;

		const uint64_t pitch = uint64_t(columns) * texelBytes;
		image.pixels         = core::fixed_buffer<std::byte>(pitch * rows);
		image.subresources.push_back({ 0, pitch, pitch * rows });
		return image;
	}

	void
	WritePositionRow(
		assetlib::ImageData&       image,
		uint32_t                   row,
		std::span<const glm::vec3> corners,
		glm::vec3                  boundsMin,
		glm::vec3                  boundsMax)
	{
		auto* texel = reinterpret_cast<uint16_t*>(
			image.pixels.data() + uint64_t(row) * image.subresources[0].rowPitch);

		const glm::vec3 extent = boundsMax - boundsMin;
		for (const glm::vec3& corner : corners)
		{
			const glm::vec3 packed = (corner - boundsMin) / extent;
			texel[0]               = glm::packUnorm1x16(packed.x);
			texel[1]               = glm::packUnorm1x16(packed.y);
			texel[2]               = glm::packUnorm1x16(packed.z);
			texel[3]               = 65535;
			texel += 4;
		}
	}

	assetlib::ImageData
	MakeFlatNormalTexture(uint32_t columns, uint32_t rows)
	{
		auto image = MakeImage(columns, rows, assetlib::VkFormat::R8G8B8A8_UNORM, 4);
		for (size_t i = 0; i < image.pixels.size(); i += 4)
		{
			image.pixels[i + 0] = std::byte{ 128 };
			image.pixels[i + 1] = std::byte{ 128 };
			image.pixels[i + 2] = std::byte{ 255 };
			image.pixels[i + 3] = std::byte{ 255 };
		}
		return image;
	}

	const std::array<glm::vec3, 4> c_QuadAtOrigin = { {
		{ -1.0f, -1.0f, 0.0f },
		{ 1.0f, -1.0f, 0.0f },
		{ -1.0f, 1.0f, 0.0f },
		{ 1.0f, 1.0f, 0.0f },
	} };

	namespace
	{
		constexpr uint32_t c_Columns = 4;
		constexpr uint32_t c_Rows    = 3;

		const glm::vec3 c_BoundsMin(-1.5f, -1.5f, -1.0f);
		const glm::vec3 c_BoundsMax(2.5f, 1.5f, 1.0f);

		assetlib::ImageData
		MakePositionTexture()
		{
			auto image = MakeImage(c_Columns, c_Rows, assetlib::VkFormat::R16G16B16A16_UNORM, 8);

			auto moved = c_QuadAtOrigin;
			for (glm::vec3& corner : moved)
			{
				corner.x += c_Step;
			}

			WritePositionRow(image, 0, c_QuadAtOrigin, c_BoundsMin, c_BoundsMax);
			WritePositionRow(image, 1, moved, c_BoundsMin, c_BoundsMax);
			WritePositionRow(image, 2, moved, c_BoundsMin, c_BoundsMax);  // the pad
			return image;
		}

		std::vector<VatVertex>
		MakeQuadVertices()
		{
			auto verts = std::vector<VatVertex>(4);
			for (size_t i = 0; i < 4; ++i)
			{
				verts[i].position = c_QuadAtOrigin[i];
				verts[i].normal   = glm::vec3(0.0f, 0.0f, 1.0f);
				verts[i].uv       = glm::vec2(i % 2 == 0 ? 0.0f : 1.0f, i < 2 ? 1.0f : 0.0f);
				verts[i].tangent  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
			}
			return verts;
		}
	}

	GeomHandle
	AddSlidingQuadGeom(IScene& scene, MaterialHandle material)
	{
		auto desc      = VatGeomDesc();
		desc.positions = scene.AddTextureAsset(MakePositionTexture(), "vat-sliding-quad-positions");
		desc.normals   = scene.AddTextureAsset(
			MakeFlatNormalTexture(c_Columns, c_Rows),
			"vat-sliding-quad-normals");
		desc.boundsMin = c_BoundsMin;
		desc.boundsMax = c_BoundsMax;
		desc.clips     = { { 0, 2, c_SampleRate, true }, { 0, 2, c_SampleRate, false } };

		const auto verts   = MakeQuadVertices();
		const auto indices = std::array<uint32_t, 6>{ { 0, 1, 2, 2, 1, 3 } };
		return scene.AddVatMeshGeom(verts, indices, desc, material);
	}
}
