#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VatSynth.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

// A VAT frame's normal map must shade exactly as the same quad posed by its instance transform
// does, because the tangent frame the pixel stage builds is then the same world frame either way.
// The static plane is the reference: its tangent is authored and transformed, so what the VAT
// stage rebuilds from the bind tangent, the baked normal and the baked twist is diffed against it.

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// The poses, as the rotation of a +Z-facing quad whose tangent is +X. The quarter turn about Z
	// leaves the normal alone and only the tangent moves -- the shortest arc sees nothing, and the
	// whole pose rides the twist. The third tilts the quad about Y and turns it the other way.
	const float c_Tilt = glm::radians(40.0f);

	const std::array<glm::mat3, 3> c_Poses = { {
		glm::mat3(1.0f),
		glm::mat3(glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f))),
		glm::mat3(
			glm::rotate(glm::mat4(1.0f), c_Tilt, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f))),
	} };

	// Each pose is its own clip of one frame (plus the pad row), so an instance freezes on it.
	constexpr uint32_t c_Columns = 4;
	constexpr uint32_t c_Rows    = 6;

	const glm::vec3 c_BoundsMin(-1.5f);
	const glm::vec3 c_BoundsMax(1.5f);

	using bgl::test::vat_synth::c_QuadAtOrigin;
	using bgl::test::vat_synth::MakeImage;

	/**
	 * The twist the bake would write for `pose`: the bind tangent carried onto the posed normal by
	 * the shortest arc, against the tangent the pose actually produced, about that normal. A copy
	 * of assetlib's vat_tangent.h, which bgl_tests cannot include; that header is the one to change.
	 */
	float
	TwistOf(const glm::mat3& pose)
	{
		const glm::vec3 bindNormal(0.0f, 0.0f, 1.0f);
		const glm::vec3 bindTangent(1.0f, 0.0f, 0.0f);
		const glm::vec3 n = pose * bindNormal;
		const glm::vec3 t = pose * bindTangent;

		const float     c       = glm::dot(bindNormal, n);
		const glm::vec3 axis    = glm::cross(bindNormal, n);
		const glm::vec3 carried = c < -1.0f + 1e-4f ?
		                              bindTangent :
		                              bindTangent * c + glm::cross(axis, bindTangent) +
		                                  axis * (glm::dot(axis, bindTangent) / (1.0f + c));

		return std::atan2(glm::dot(t, glm::cross(n, carried)), glm::dot(t, carried));
	}

	assetlib::ImageData
	MakePositionTexture()
	{
		auto image = MakeImage(c_Columns, c_Rows, assetlib::VkFormat::R16G16B16A16_UNORM, 8);
		for (size_t p = 0; p < c_Poses.size(); ++p)
		{
			auto corners = c_QuadAtOrigin;
			for (glm::vec3& corner : corners) corner = c_Poses[p] * corner;

			for (uint32_t row = 0; row < 2; ++row)
				bgl::test::vat_synth::WritePositionRow(
					image,
					static_cast<uint32_t>(p * 2 + row),
					corners,
					c_BoundsMin,
					c_BoundsMax);
		}
		return image;
	}

	assetlib::ImageData
	MakeNormalTwistTexture()
	{
		auto image = MakeImage(c_Columns, c_Rows, assetlib::VkFormat::R8G8B8A8_UNORM, 4);
		for (size_t p = 0; p < c_Poses.size(); ++p)
		{
			const glm::vec3 n     = c_Poses[p] * glm::vec3(0.0f, 0.0f, 1.0f);
			const float     alpha = TwistOf(c_Poses[p]) / glm::two_pi<float>() + 0.5f;

			for (uint32_t row = 0; row < 2; ++row)
			{
				auto* texel =
					image.pixels.data() + uint64_t(p * 2 + row) * image.subresources[0].rowPitch;
				for (uint32_t column = 0; column < c_Columns; ++column)
				{
					texel[0] = std::byte(glm::packUnorm1x8(n.x * 0.5f + 0.5f));
					texel[1] = std::byte(glm::packUnorm1x8(n.y * 0.5f + 0.5f));
					texel[2] = std::byte(glm::packUnorm1x8(n.z * 0.5f + 0.5f));
					texel[3] = std::byte(glm::packUnorm1x8(alpha));
					texel += 4;
				}
			}
		}
		return image;
	}

	/** A normal map leaning hard along the tangent: every texel is (+0.9, 0, +0.44). */
	assetlib::ImageData
	MakeLeaningNormalMap()
	{
		auto image = MakeImage(4, 4, assetlib::VkFormat::R8G8B8A8_UNORM, 4);
		for (size_t i = 0; i < image.pixels.size(); i += 4)
		{
			image.pixels[i + 0] = std::byte{ 242 };  // 0.95 -> x = +0.9
			image.pixels[i + 1] = std::byte{ 128 };  // 0.5  -> y = 0
			image.pixels[i + 2] = std::byte{ 255 };
			image.pixels[i + 3] = std::byte{ 255 };
		}
		return image;
	}

	std::vector<bgl::VatVertex>
	MakeQuadVertices()
	{
		auto verts = std::vector<bgl::VatVertex>(4);
		for (size_t i = 0; i < 4; ++i)
		{
			verts[i].position = c_QuadAtOrigin[i];
			verts[i].normal   = glm::vec3(0.0f, 0.0f, 1.0f);
			verts[i].uv       = glm::vec2(i % 2 == 0 ? 0.0f : 1.0f, i < 2 ? 1.0f : 0.0f);
			verts[i].tangent  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		}
		return verts;
	}

	float
	MaxChannelDelta(const bgl::test::Rgba& a, const bgl::test::Rgba& b)
	{
		return std::max({ std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b) });
	}
}

TEST_CASE("A VAT frame's normal map shades like the same quad posed statically", "[vat][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;
	material.normalTexture   = scene->AddTextureAsset(MakeLeaningNormalMap(), "leaning-normals");
	const auto pbr           = scene->CreatePbrMaterial(material);

	auto desc      = bgl::VatGeomDesc();
	desc.positions = scene->AddTextureAsset(MakePositionTexture(), "vat-positions");
	desc.normals   = scene->AddTextureAsset(MakeNormalTwistTexture(), "vat-normals");
	desc.boundsMin = c_BoundsMin;
	desc.boundsMax = c_BoundsMax;
	desc.clips     = { { 0, 1, 30.0f, false }, { 2, 1, 30.0f, false }, { 4, 1, 30.0f, false } };

	const auto verts   = MakeQuadVertices();
	const auto indices = std::array<uint32_t, 6>{ { 0, 1, 2, 2, 1, 3 } };
	const auto vat     = scene->AddVatMeshGeom(verts, indices, desc, pbr);
	const auto plane   = scene->AddPlaneGeom(1, 1, 2.0f, 2.0f, pbr);

	// Three columns, one per pose, drawn twice over the same pixels: first the VAT quad frozen on
	// its clip, then the plane carried into the same pose by its transform. Two frames rather than
	// two rows, because a row's height changes the view vector the specular term sees.
	const std::array<float, 3> columnX = { { -3.2f, 0.0f, 3.2f } };

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	std::array<bgl::MeshInstanceHandle, 3> vatInstances;
	for (uint32_t p = 0; p < 3; ++p)
	{
		const auto at = glm::translate(glm::mat4(1.0f), glm::vec3(columnX[p], 0.0f, 0.0f));
		vatInstances[p] =
			view->CreateVatMeshInstance(vat, at, bgl::VatInstanceDesc{ p, 0.0f, 0.0f });
	}

	gfx->DrawFrame(target, job);
	const std::string got = "assets/golden/vat_normal_map.got.png";
	gfx->ScreenshotPng(target, got);

	for (uint32_t p = 0; p < 3; ++p)
	{
		view->DeleteMeshInstance(vatInstances[p]);
		const auto at = glm::translate(glm::mat4(1.0f), glm::vec3(columnX[p], 0.0f, 0.0f));
		view->CreateStaticMeshInstance(plane, at * glm::mat4(c_Poses[p]));
	}

	gfx->DrawFrame(target, job);
	const std::string reference = "assets/golden/vat_normal_map_reference.got.png";
	gfx->ScreenshotPng(target, reference);

	// ~52 px per world unit at 10 units from the camera: screen x = 400 + 52x, and y = 300.
	const auto centre = [&](const std::string& path, uint32_t p) {
		const int sx = static_cast<int>(400.0f + 52.0f * columnX[p]);
		return bgl::test::MeanColor(path, sx - 8, 292, 16, 16);
	};

	std::array<bgl::test::Rgba, 3> vatColour;
	std::array<bgl::test::Rgba, 3> planeColour;
	for (uint32_t p = 0; p < 3; ++p)
	{
		vatColour[p]   = centre(got, p);
		planeColour[p] = centre(reference, p);
		CHECK(vatColour[p].Luma() > 0.05f);
		CHECK(planeColour[p].Luma() > 0.05f);
	}

	// The scene must be able to tell the poses apart, or agreement below proves nothing: the map
	// leans the shading normal along the tangent, and the quarter turn swings that lean from world
	// +X to world +Y.
	CHECK(MaxChannelDelta(planeColour[0], planeColour[1]) > 0.02f);

	// Each VAT pose shades as its statically posed twin -- within what an 8-bit normal and twist
	// can place the frame.
	for (uint32_t p = 0; p < 3; ++p)
	{
		INFO("pose " << p);
		CHECK(MaxChannelDelta(vatColour[p], planeColour[p]) < 0.012f);
	}

	std::filesystem::remove(reference);
	CHECK(bgl::test::MatchesGolden("assets/golden/vat_normal_map.exp.png", got));
}
