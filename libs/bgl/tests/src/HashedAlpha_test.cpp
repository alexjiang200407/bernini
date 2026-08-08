#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib_structs/ImageData.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

namespace
{
	constexpr uint32_t c_Width  = 256;
	constexpr uint32_t c_Height = 256;

	constexpr float c_HalfFov = 30.0f;

	/**
	 * The frame a scene is rendered into, and how much of the world it takes in.
	 *
	 * Every bound in this file and in docs/taa.md was measured at one resolution, so nothing here
	 * could see an artifact that only appears at another -- which is what a 1080p display is next to
	 * a 4K one. `fovScale` is what separates the two reasons a lower resolution changes the image:
	 * at 1 the camera is unchanged and the content shrinks in pixels, which is the display; below 1
	 * the field of view narrows with the frame, so the content keeps its pixel footprint and only
	 * the frame is smaller, which leaves the resolve as the only thing that varied.
	 */
	struct Frame
	{
		uint32_t width    = c_Width;
		uint32_t height   = c_Height;
		float    fovScale = 1.0f;

		[[nodiscard]] float
		TanHalfFov() const noexcept
		{
			return std::tan(glm::radians(c_HalfFov)) * fovScale;
		}

		[[nodiscard]] float
		VerticalFov() const noexcept
		{
			return 2.0f * std::atan(TanHalfFov());
		}

		[[nodiscard]] float
		Aspect() const noexcept
		{
			return static_cast<float>(width) / static_cast<float>(height);
		}
	};

	// A box well inside the plane's silhouette, so every pixel in it is a coverage decision rather
	// than an edge.
	constexpr int c_BoxX = 88;
	constexpr int c_BoxY = 88;
	constexpr int c_Box  = 80;

	// Several times the resolve's 1/`c_BlendWeight` time constant, so the accumulation has settled
	constexpr int c_ConvergeFrames = 100;

	// The band just below the horizon, where a pixel's footprint on the receding plane is long along
	// the view axis and short across it. Wide and short, because that is the shape of the region --
	// the foreground half is already near-isotropic and would dilute the measurement.
	constexpr int c_GrazeX = 28;
	constexpr int c_GrazeY = 130;
	constexpr int c_GrazeW = 200;
	constexpr int c_GrazeH = 22;

	// The strafe the grazing flicker measurement pans with: about a pixel of screen motion per
	// frame over the band.
	constexpr float c_GrazePanStep   = 0.05f;
	constexpr int   c_GrazePanWarmup = 40;

	// One scene, its target, and the job that keeps drawing it. Held together because a test that
	// captures more than one frame has to drive the same target across them -- a fresh stack per
	// frame would start the accumulation over each time.
	struct PatchScene
	{
		bgl::GraphicsRef     gfx;
		bgl::RenderTargetRef target;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderJob       job;
	};

	bgl::Camera
	GrazingCameraAt(float x)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(x, 1.5f, 0.0f),
				glm::vec3(x, 1.4f, -10.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.1f,
				500.0f);
		return camera;
	}

	// A plane laid flat and viewed almost edge-on, so one pixel spans a long stretch of it along the
	// view axis and almost none across -- the anisotropy a hair card sits in for most of its area.
	PatchScene
	MakeGrazingScene(bgl::LayerType layer, float alpha, bool taaEnabled)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 8;
		sceneDesc.initialVertexBufferByteSize = 800000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		desc.layerType       = layer;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 400.0f, 400.0f, material);

		// Laid flat, so the camera just above it sees it recede to the horizon.
		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = GrazingCameraAt(0.0f);
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		return PatchScene{ gfx, target, scene, view, job };
	}

	bgl::test::Rgba
	RenderGrazing(const std::string& path, bgl::LayerType layer, float alpha)
	{
		PatchScene grazing = MakeGrazingScene(layer, alpha, false);

		grazing.gfx->DrawFrame(grazing.target, grazing.job);
		grazing.gfx->ScreenshotPng(grazing.target, path);
		return bgl::test::MeanColor(path, c_GrazeX, c_GrazeY, c_GrazeW, c_GrazeH);
	}

	// The temporal counterpart of RenderGrazing: the same plane drawn under TAA for `warmup` frames
	// with the camera strafing `panStep` sideways each frame -- zero holds it still -- then two
	// consecutive frames captured and differenced over the grazing band.
	float
	GrazingFrameDelta(
		const std::string& pathA,
		const std::string& pathB,
		bgl::LayerType     layer,
		float              alpha,
		int                warmup,
		float              panStep)
	{
		PatchScene grazing = MakeGrazingScene(layer, alpha, true);

		for (int frame = 0; frame < warmup; ++frame)
		{
			grazing.job.camera = GrazingCameraAt(-static_cast<float>(warmup - 1 - frame) * panStep);
			grazing.gfx->DrawFrame(grazing.target, grazing.job);
		}

		grazing.gfx->ScreenshotPng(grazing.target, pathA);

		grazing.job.camera = GrazingCameraAt(panStep);
		grazing.gfx->DrawFrame(grazing.target, grazing.job);
		grazing.gfx->ScreenshotPng(grazing.target, pathB);

		return bgl::test::FrameDelta(pathA, pathB, c_GrazeX, c_GrazeY, c_GrazeW, c_GrazeH);
	}

	// One plane at `alpha` facing the camera, filling the middle of the frame.
	PatchScene
	MakePatchScene(bgl::LayerType layer, float alpha, bool taaEnabled, float planeSize = 12.0f)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 8;
		sceneDesc.initialVertexBufferByteSize = 800000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);

		// PBR does not render without an environment; there is no default.
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		desc.layerType       = layer;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, planeSize, planeSize, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		return PatchScene{ gfx, target, scene, view, job };
	}

	// Vertical strands two texels wide every 16, opaque on a transparent background -- a hair
	// texture's flyaway region, reduced to what matters. The strand covers an eighth of its period,
	// so plain box mips dilute its alpha by half per level once the strand is sub-texel.
	constexpr uint32_t c_StrandTexSize = 256;
	constexpr uint32_t c_StrandWidth   = 2;
	constexpr uint32_t c_StrandPeriod  = 16;

	// The two mip policies a base colour can reach the shader with: plain box mips, which is what
	// blend bakes (and what hashed wrongly got), and coverage-preserving mips, which rescale each
	// level's alpha so the fraction passing 0.5 matches mip 0's -- the bake's kMask/kHashed path,
	// reproduced here because the baked file is BC7 and cannot be authored directly in a test.
	// `alpha` is mip 0, one float per texel; the chain is box-filtered down from it in float so
	// the rescale is not quantized away.
	assetlib::ImageData
	MippedAlphaTexture(std::vector<float> alpha, bool preserveCoverage, bool binary = false)
	{
		auto image      = assetlib::ImageData();
		image.width     = c_StrandTexSize;
		image.height    = c_StrandTexSize;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_SRGB;
		image.isCubemap = false;

		image.mipLevels = 1;
		for (uint32_t dim = c_StrandTexSize; dim > 1; dim >>= 1)
		{
			++image.mipLevels;
		}

		size_t totalBytes = 0;
		for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
		{
			const uint32_t dim = std::max(1u, c_StrandTexSize >> mip);
			totalBytes += static_cast<size_t>(dim) * dim * 4;
		}
		image.pixels = core::fixed_buffer<std::byte>(totalBytes);

		const double mip0Coverage =
			static_cast<double>(
				std::count_if(alpha.begin(), alpha.end(), [](float a) { return a >= 0.5f; })) /
			static_cast<double>(alpha.size());

		size_t offset = 0;
		for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
		{
			const uint32_t dim = std::max(1u, c_StrandTexSize >> mip);

			std::vector<float> level = alpha;
			if (preserveCoverage && mip > 0)
			{
				// The smallest scale whose coverage still reaches mip 0's, like the bake's
				// matchAlphaCoverage.
				float lo = 0.0f;
				float hi = 8.0f;
				for (int i = 0; i < 12; ++i)
				{
					const float mid   = 0.5f * (lo + hi);
					size_t      count = 0;
					for (const float a : alpha)
					{
						if (std::min(1.0f, a * mid) >= 0.5f)
						{
							++count;
						}
					}
					const double coverage =
						static_cast<double>(count) / static_cast<double>(alpha.size());
					(coverage < mip0Coverage ? lo : hi) = mid;
				}
				for (float& a : level)
				{
					a = std::min(1.0f, a * hi);
				}
			}

			// A hard-painted mask stays 0-or-1 down its whole rescaled chain (angelica's measures
			// binary at every level); the bisection above instead parks soft content exactly at the
			// cutoff, a knife edge no real bake produces from such sources.
			if (binary && mip > 0)
			{
				for (float& a : level)
				{
					a = a >= 0.5f ? 1.0f : 0.0f;
				}
			}

			for (uint32_t y = 0; y < dim; ++y)
			{
				for (uint32_t x = 0; x < dim; ++x)
				{
					const size_t t = offset + (static_cast<size_t>(y) * dim + x) * 4;

					image.pixels[t + 0] = std::byte{ 140 };
					image.pixels[t + 1] = std::byte{ 100 };
					image.pixels[t + 2] = std::byte{ 70 };
					image.pixels[t + 3] = std::byte{ static_cast<uint8_t>(
						level[static_cast<size_t>(y) * dim + x] * 255.0f + 0.5f) };
				}
			}

			image.subresources.push_back(
				{ offset, static_cast<uint64_t>(dim) * 4, static_cast<uint64_t>(dim) * dim * 4 });
			offset += static_cast<size_t>(dim) * dim * 4;

			// Box-filter down for the next level.
			if (dim > 1)
			{
				const uint32_t     next = dim >> 1;
				std::vector<float> down(static_cast<size_t>(next) * next);
				for (uint32_t y = 0; y < next; ++y)
				{
					for (uint32_t x = 0; x < next; ++x)
					{
						const size_t a = static_cast<size_t>(y * 2) * dim + x * 2;
						const size_t b = a + 1;
						const size_t c = a + dim;
						const size_t d = c + 1;
						down[static_cast<size_t>(y) * next + x] =
							0.25f * (alpha[a] + alpha[b] + alpha[c] + alpha[d]);
					}
				}
				alpha = std::move(down);
			}
		}

		return image;
	}

	// Mean alpha of one mip level, read off the image the scene will upload. Under stochastic
	// coverage this is the fraction of a fully lit patch the ensemble should converge to once the
	// footprint sits at that level -- whatever the coverage rescale did to it included.
	float
	MeanMipAlpha(const assetlib::ImageData& image, uint32_t mip)
	{
		const uint32_t dim    = std::max(1u, image.width >> mip);
		const size_t   offset = image.subresources[mip].offset;

		size_t sum = 0;
		for (size_t t = 0; t < static_cast<size_t>(dim) * dim; ++t)
		{
			sum += static_cast<uint8_t>(image.pixels[offset + t * 4 + 3]);
		}
		return static_cast<float>(sum) /
		       (255.0f * static_cast<float>(dim) * static_cast<float>(dim));
	}

	// Vertical strands two texels wide every 16, opaque on a transparent background -- a hair
	// texture's flyaway region, reduced to what matters. The strand covers an eighth of its
	// period, so plain box mips dilute its alpha by half per level once the strand is sub-texel.
	assetlib::ImageData
	MakeStrandTexture(bool preserveCoverage, bool binary = false)
	{
		std::vector<float> alpha(static_cast<size_t>(c_StrandTexSize) * c_StrandTexSize);
		for (uint32_t y = 0; y < c_StrandTexSize; ++y)
		{
			for (uint32_t x = 0; x < c_StrandTexSize; ++x)
			{
				alpha[static_cast<size_t>(y) * c_StrandTexSize + x] =
					(x % c_StrandPeriod) < c_StrandWidth ? 1.0f : 0.0f;
			}
		}

		return MippedAlphaTexture(std::move(alpha), preserveCoverage, binary);
	}

	// Alpha rising linearly from nothing at either side edge to opaque at the vertical midline --
	// a hair card's tip region, reduced to what matters: a band holding every partial coverage at
	// once, so the stochastic zone is spatially wide instead of a thin silhouette fringe.
	assetlib::ImageData
	MakeRampTexture()
	{
		std::vector<float> alpha(static_cast<size_t>(c_StrandTexSize) * c_StrandTexSize);
		for (uint32_t y = 0; y < c_StrandTexSize; ++y)
		{
			for (uint32_t x = 0; x < c_StrandTexSize; ++x)
			{
				const float u =
					(static_cast<float>(x) + 0.5f) / static_cast<float>(c_StrandTexSize);
				alpha[static_cast<size_t>(y) * c_StrandTexSize + x] =
					std::clamp(1.0f - std::abs(u - 0.5f) * 2.0f, 0.0f, 1.0f);
			}
		}

		return MippedAlphaTexture(std::move(alpha), true);
	}

	// The strand plane on screen: small enough that a strand is sub-pixel and the active mip is the
	// diluted regime a distant hair card samples.
	constexpr float c_StrandPlane = 5.0f;

	constexpr int c_StrandBoxX = 104;
	constexpr int c_StrandBoxY = 104;
	constexpr int c_StrandBox  = 48;

	// A backdrop bright enough that a strand-coloured remnant on it is measurable, far enough
	// behind the strands that a camera pan separates their motions -- hair over a lit scene,
	// reduced to what matters.
	constexpr float c_BackdropZ    = -6.0f;
	constexpr float c_BackdropSize = 40.0f;

	PatchScene
	MakeCardScene(
		assetlib::ImageData texture,
		bool                taaEnabled   = true,
		bool                withBackdrop = false,
		float               cameraZ      = 20.0f,
		bgl::LayerType      layer        = bgl::LayerType::kHashed,
		Frame               frame        = {})
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(frame.width);
		targetDesc.height     = static_cast<int>(frame.height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 8;
		sceneDesc.initialVertexBufferByteSize = 800000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto desc             = bgl::PbrMaterialDesc();
		desc.baseColorFactor  = glm::vec4(1.0f);
		desc.metallicFactor   = 0.0f;
		desc.roughnessFactor  = 0.6f;
		desc.layerType        = layer;
		desc.baseColorTexture = scene->AddTextureAsset(std::move(texture), "strands");

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, c_StrandPlane, c_StrandPlane, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		if (withBackdrop)
		{
			auto grey            = bgl::PbrMaterialDesc();
			grey.baseColorFactor = glm::vec4(0.62f, 0.62f, 0.62f, 1.0f);
			grey.metallicFactor  = 0.0f;
			grey.roughnessFactor = 0.6f;

			auto backdrop = scene->AddPlaneGeom(
				1,
				1,
				c_BackdropSize,
				c_BackdropSize,
				scene->CreatePbrMaterial(grey));
			view->CreateStaticMeshInstance(
				backdrop,
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, c_BackdropZ)));
		}

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, cameraZ),
				glm::vec3(0.0f, 0.0f, cameraZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(frame.VerticalFov(), frame.Aspect(), 0.5f, 500.0f);

		auto job   = bgl::RenderJob();
		job.view   = view;
		job.camera = camera;
		job.viewport =
			bgl::Viewport(static_cast<float>(frame.width), static_cast<float>(frame.height));

		return PatchScene{ gfx, target, scene, view, job };
	}

	// The centred box covering 60% of the strand plane's on-screen span, whatever the frame does to
	// that span. A fraction of the frame would drift off the plane as the field of view narrows.
	struct StrandBox
	{
		int xy   = 0;
		int size = 0;
	};

	StrandBox
	StrandBoxFor(const Frame& frame, float cameraZ)
	{
		const float spanPx = c_StrandPlane / (2.0f * cameraZ * frame.TanHalfFov()) *
		                     static_cast<float>(frame.height);

		const int size = static_cast<int>(spanPx * 0.6f);
		return StrandBox{ (static_cast<int>(frame.width) - size) / 2, size };
	}

	// Converges the strand scene, then captures two consecutive frames: the delta is flicker, and
	// the mean is how much of the strands' footprint survived to the screen.
	std::pair<float, bgl::test::Rgba>
	StrandFlicker(const std::string& pathA, const std::string& pathB, bool preserveCoverage)
	{
		PatchScene strands = MakeCardScene(MakeStrandTexture(preserveCoverage));

		for (int frame = 0; frame < c_ConvergeFrames; ++frame)
		{
			strands.gfx->DrawFrame(strands.target, strands.job);
		}

		strands.gfx->ScreenshotPng(strands.target, pathA);

		strands.gfx->DrawFrame(strands.target, strands.job);
		strands.gfx->ScreenshotPng(strands.target, pathB);

		const float delta = bgl::test::FrameDelta(
			pathA,
			pathB,
			c_StrandBoxX,
			c_StrandBoxY,
			c_StrandBox,
			c_StrandBox);
		const bgl::test::Rgba mean =
			bgl::test::MeanColor(pathA, c_StrandBoxX, c_StrandBoxY, c_StrandBox, c_StrandBox);

		return { delta, mean };
	}

	// Renders one plane at `alpha` with the given layer, `frames` times, and returns the mean colour
	// of the patch. The background is black, so a discarded fragment contributes nothing and the mean
	// over the patch is the surviving fraction times what a fully covered patch would read.
	bgl::test::Rgba
	RenderPatch(
		const std::string& path,
		bgl::LayerType     layer,
		float              alpha,
		bool               taaEnabled = false,
		int                frames     = 1)
	{
		PatchScene patch = MakePatchScene(layer, alpha, taaEnabled);

		for (int frame = 0; frame < frames; ++frame)
		{
			patch.gfx->DrawFrame(patch.target, patch.job);
		}

		patch.gfx->ScreenshotPng(patch.target, path);
		return bgl::test::MeanColor(path, c_BoxX, c_BoxY, c_Box, c_Box);
	}

	// Sideways camera slide per frame while panning over the patch, and how long the accumulation is
	// given to reach its moving steady state. The travel it adds up to must stay inside the margin a
	// `planeSize` of 48 leaves around the frame, so the sample box never sees the plane's edge.
	constexpr float c_PanStep   = 0.2f;
	constexpr int   c_PanWarmup = 40;
	constexpr float c_PanPlane  = 48.0f;

	bgl::Camera
	PanCameraAt(float x)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(x, 0.0f, 20.0f),
				glm::vec3(x, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);
		return camera;
	}

	// The moving counterpart of ConsecutiveFrameDelta: the camera slides throughout, including
	// between the two captures, so what separates them is flicker under motion.
	float
	PanningFrameDelta(
		const std::string& pathA,
		const std::string& pathB,
		bgl::LayerType     layer,
		float              alpha)
	{
		PatchScene patch = MakePatchScene(layer, alpha, true, c_PanPlane);

		for (int frame = 0; frame < c_PanWarmup; ++frame)
		{
			patch.job.camera =
				PanCameraAt(-static_cast<float>(c_PanWarmup - 1 - frame) * c_PanStep);
			patch.gfx->DrawFrame(patch.target, patch.job);
		}

		patch.gfx->ScreenshotPng(patch.target, pathA);

		patch.job.camera = PanCameraAt(c_PanStep);
		patch.gfx->DrawFrame(patch.target, patch.job);
		patch.gfx->ScreenshotPng(patch.target, pathB);

		return bgl::test::FrameDelta(pathA, pathB, c_BoxX, c_BoxY, c_Box, c_Box);
	}

	// Draws `warmup` frames, captures, draws one more and captures again -- two consecutive frames of
	// one accumulation, with the camera never moving. What separates them is flicker and nothing else.
	float
	ConsecutiveFrameDelta(
		const std::string& pathA,
		const std::string& pathB,
		bgl::LayerType     layer,
		float              alpha,
		int                warmup)
	{
		PatchScene patch = MakePatchScene(layer, alpha, true);

		for (int frame = 0; frame < warmup; ++frame)
		{
			patch.gfx->DrawFrame(patch.target, patch.job);
		}

		patch.gfx->ScreenshotPng(patch.target, pathA);

		patch.gfx->DrawFrame(patch.target, patch.job);
		patch.gfx->ScreenshotPng(patch.target, pathB);

		return bgl::test::FrameDelta(pathA, pathB, c_BoxX, c_BoxY, c_Box, c_Box);
	}

	// The pan the strand smear is measured under. Fast enough (about two pixels a frame at this
	// camera) that a trailing ghost has room to show inside the band before the clamp scrubs it.
	constexpr float c_SmearPanStep   = 0.2f;
	constexpr int   c_SmearPanFrames = 40;

	// Bands inside the ramp's two stochastic zones. The card spans x = 100 to 156 at this camera
	// with the ramp opaque at its midline, so each band covers partial coverage from about a
	// quarter to seven eighths. `trail` is the side the pan drags history across; `lead` mirrors
	// it on the side being uncovered. Softness the motion adds legitimately is symmetric between
	// them; a ghost is not.
	constexpr int c_TrailBandX = 132;
	constexpr int c_LeadBandX  = 104;
	constexpr int c_BandW      = 20;
	constexpr int c_BandY      = 108;
	constexpr int c_BandH      = 40;

	struct SmearBands
	{
		float trail;
		float lead;

		// The same bands between two consecutive frames of the converged still: the residual the
		// stochastic coverage keeps even at rest, which is the zero the figures above are read
		// against -- the deterministic TAA-off control cannot supply it.
		float trailFloor;
		float leadFloor;
	};

	// Converges one strand accumulation still at the destination camera, pans a second one to
	// arrive at the same camera, and differences the two over each band -- the backdrop the two
	// renders share cancels out, so a band reads what the pan itself left there.
	SmearBands
	StrandSmear(const std::string& stillPath, const std::string& pannedPath, bool taaEnabled)
	{
		const std::string stillNextPath = stillPath + ".next.png";

		PatchScene still = MakeCardScene(MakeRampTexture(), taaEnabled, true);
		for (int frame = 0; frame < c_ConvergeFrames; ++frame)
		{
			still.gfx->DrawFrame(still.target, still.job);
		}
		still.gfx->ScreenshotPng(still.target, stillPath);

		still.gfx->DrawFrame(still.target, still.job);
		still.gfx->ScreenshotPng(still.target, stillNextPath);

		PatchScene panned = MakeCardScene(MakeRampTexture(), taaEnabled, true);
		for (int frame = 0; frame < c_SmearPanFrames; ++frame)
		{
			panned.job.camera =
				PanCameraAt(-static_cast<float>(c_SmearPanFrames - 1 - frame) * c_SmearPanStep);
			panned.gfx->DrawFrame(panned.target, panned.job);
		}
		panned.gfx->ScreenshotPng(panned.target, pannedPath);

		const auto band = [&](const std::string& path, int x) {
			return bgl::test::FrameDelta(path, stillPath, x, c_BandY, c_BandW, c_BandH);
		};

		return SmearBands{ band(pannedPath, c_TrailBandX),
			               band(pannedPath, c_LeadBandX),
			               band(stillNextPath, c_TrailBandX),
			               band(stillNextPath, c_LeadBandX) };
	}
}

// The contract, and the whole reason the technique works: a fragment survives with probability equal
// to its alpha. Measured against the same material at kOpaque, which is what full coverage reads --
// so this is a ratio and owes nothing to the shading, the exposure or the tonemap.
//
// Two alphas, because a single one could be matched by a threshold that ignored alpha and happened
// to land near it.
TEST_CASE(
	"Hashed alpha keeps the fraction of fragments its alpha asks for",
	"[hashedalpha][render]")
{
	const bgl::test::Rgba full =
		RenderPatch("assets/golden/hashed_alpha_opaque.got.png", bgl::LayerType::kOpaque, 1.0f);

	// Well clear of the background, or every ratio below divides by noise.
	REQUIRE(full.Luma() > 0.1f);

	const float alphas[] = { 0.25f, 0.75f };
	for (const float alpha : alphas)
	{
		const std::string path =
			std::format("assets/golden/hashed_alpha_{}.got.png", static_cast<int>(alpha * 100.0f));

		const bgl::test::Rgba patch = RenderPatch(path, bgl::LayerType::kHashed, alpha);
		const float           ratio = patch.Luma() / full.Luma();

		INFO("alpha = " << alpha << ", survived = " << ratio);

		// One frame of a stochastic pattern over 80x80 pixels: the sampling error on the mean is a
		// few percent, and the margin is what separates "equals alpha" from "correlates with it".
		CHECK(ratio == Catch::Approx(alpha).margin(0.06));
	}
}

// A pattern that did not move between frames would leave temporal AA converging to one sample of the
// distribution rather than through it -- the noise would resolve to itself and stay. The seed is what
// moves it, and it only moves on a target that has an accumulation to integrate it.
TEST_CASE("The hashed pattern changes between frames, but only under TAA", "[hashedalpha][render]")
{
	const std::string first  = "assets/golden/hashed_alpha_seed0.got.png";
	const std::string second = "assets/golden/hashed_alpha_seed1.got.png";

	SECTION("with temporal AA the pattern advances")
	{
		// Two frames rather than one, so the second sees a different seed. Compared before the
		// accumulation has had time to smooth them together.
		RenderPatch(first, bgl::LayerType::kHashed, 0.5f, true, 1);
		RenderPatch(second, bgl::LayerType::kHashed, 0.5f, true, 2);

		CHECK_FALSE(bgl::test::MatchesGolden(first, second));
	}

	SECTION("without it the pattern is still")
	{
		// A moving pattern with nothing accumulating it is flicker, so a target without TAA must
		// draw the same fragments every frame.
		RenderPatch(first, bgl::LayerType::kHashed, 0.5f, false, 1);
		RenderPatch(second, bgl::LayerType::kHashed, 0.5f, false, 2);

		CHECK(bgl::test::MatchesGolden(first, second));
	}
}

// The claim that makes the technique usable at all: the noise is something TAA integrates away. A
// converged patch must be far smoother than a single frame of the same pattern, and must still be
// partial coverage rather than having drifted to opaque or to nothing.
TEST_CASE("Temporal AA resolves the hashed noise", "[hashedalpha][render]")
{
	const std::string single    = "assets/golden/hashed_alpha_single.got.png";
	const std::string converged = "assets/golden/hashed_alpha_converged.got.png";

	const bgl::test::Rgba full =
		RenderPatch("assets/golden/hashed_alpha_full.got.png", bgl::LayerType::kOpaque, 1.0f);

	const bgl::test::Rgba one = RenderPatch(single, bgl::LayerType::kHashed, 0.5f, true, 1);
	const bgl::test::Rgba many =
		RenderPatch(converged, bgl::LayerType::kHashed, 0.5f, true, c_ConvergeFrames);

	const float noisy  = bgl::test::AliasEnergy(single, c_BoxX, c_BoxY, c_Box, c_Box);
	const float smooth = bgl::test::AliasEnergy(converged, c_BoxX, c_BoxY, c_Box, c_Box);

	INFO("patch grain: one frame = " << noisy << ", converged = " << smooth);
	INFO(
		"patch luma:  one frame = " << one.Luma() << ", converged = " << many.Luma()
									<< ", full = " << full.Luma());

	// A single frame has to be grainy in the first place, or there is nothing to resolve and the
	// comparison proves nothing.
	CHECK(noisy > 1e-3f);
	CHECK(smooth < noisy * 0.5f);

	// Still half-covered: converging to full coverage would mean the threshold stopped discarding,
	// and converging to nothing would mean it stopped keeping.
	CHECK(many.Luma() < full.Luma() * 0.95f);
	CHECK(many.Luma() > full.Luma() * 0.05f);

	// The converged patch reads *brighter* than one frame of the same coverage, and should. A single
	// frame is half black pixels and half lit ones, and the mean of their display values is below the
	// display value of their mean -- the curve is concave, so averaging in linear before it lands
	// higher. Asserting the two were equal would be asserting the tonemap is linear.
	CHECK(many.Luma() > one.Luma());
}

// Smooth is not the same as still. A patch can measure flat across its pixels every frame and be a
// different flat patch each time, which is what flicker is -- and the spatial measure above scores
// that as a success. This is the temporal half: with the camera fixed and the accumulation warm, two
// consecutive frames have to agree.
//
// The opaque patch is the floor. It draws the same fragments every frame and differs only by the
// jitter, so whatever it scores is what "still" costs here; hashed alpha is asked to come within a
// small multiple of it rather than to reach zero.
TEST_CASE("A converged hashed patch stops changing between frames", "[hashedalpha][render]")
{
	const float opaque = ConsecutiveFrameDelta(
		"assets/golden/hashed_alpha_still_opaque_a.got.png",
		"assets/golden/hashed_alpha_still_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f,
		c_ConvergeFrames);

	const float hashed = ConsecutiveFrameDelta(
		"assets/golden/hashed_alpha_still_a.got.png",
		"assets/golden/hashed_alpha_still_b.got.png",
		bgl::LayerType::kHashed,
		0.5f,
		c_ConvergeFrames);

	INFO("frame-to-frame delta: opaque = " << opaque << ", hashed = " << hashed);

	// The instrument has to be able to read zero, or the bound below is measuring its own noise floor.
	REQUIRE(opaque < 1e-5f);

	// Measured 0.0049 with a hash cell one to two pixels wide, 0.0020 once it is sub-pixel, and 0.0013
	// at a blend weight of 0.05. The bound sits between the last two, so neither the correlated pattern
	// nor a weight raised back to 0.1 can return unnoticed.
	CHECK(hashed < 1.6e-3f);
}

// The moving half of the flicker contract. A pixel where the hash discarded this frame's fragment
// carries whatever velocity was written behind the surface -- nothing, for a background at rest --
// while its history carries the surface, and reprojecting the one by the other snaps the
// accumulation every frame. The camera never exposes new geometry during the pan, so the patch
// interior should change no more than the same pan over an opaque surface, give or take the noise
// the coverage adds.
//
// The opaque pan is the floor rather than a fixed number because a moving camera changes the image
// legitimately -- specular shift, jitter -- and that cost has to be read out of the hashed figure.
TEST_CASE("A pan does not flicker a converged hashed patch", "[hashedalpha][render]")
{
	const float opaque = PanningFrameDelta(
		"assets/golden/hashed_alpha_pan_opaque_a.got.png",
		"assets/golden/hashed_alpha_pan_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f);

	const float hashed = PanningFrameDelta(
		"assets/golden/hashed_alpha_pan_a.got.png",
		"assets/golden/hashed_alpha_pan_b.got.png",
		bgl::LayerType::kHashed,
		0.5f);

	INFO("panning frame-to-frame delta: opaque = " << opaque << ", hashed = " << hashed);

	// The floor has to be small, or the bound below hides the flicker inside legitimate motion.
	REQUIRE(opaque < 1e-5f);

	// Measured 0.0029 on Apple silicon and 0.0040 on NVIDIA Ada, about two to four times the still
	// figure -- motion adds sub-pixel reprojection phase the still case never sees, and each
	// vendor's filtering pays a different price for it. The gap is not a defect: at rest the two
	// agree (0.0010 Ada, 0.0013 Apple), the opaque floor above reads zero, and a vertical pan
	// measures the same as a horizontal one. What the bound guards against is reprojection changes
	// that shake the accumulation: dilating the motion vector to the longest in the 3x3 measured
	// 0.0040-0.0073 on Apple silicon, because on stochastic coverage it re-samples the noise field
	// at a fresh fractional offset every frame. The bound sits above both healthy vendors with the
	// spread as its margin.
	CHECK(hashed < 4.5e-3f);
}

// The temporal contract at the anisotropy a hair card sits in. Head-on, the hash cells are
// near-isotropic and the test above already pins the flicker; at grazing incidence the cells are
// clamped by c_MaxAnisotropy, and if that leaves them wider than a pixel across the compressed
// axis, neighbouring pixels share a threshold, the clamp's box collapses, and the resolve snaps
// the accumulation back onto the noise -- flicker that only exists at grazing angles, which is why
// the head-on test cannot catch it.
//
// Measured at c_MaxAnisotropy = 4: still = 0.0097, panning = 0.0134 -- seven times the head-on
// figure. At 2, an octave finer -- one to two pixels across the compressed axis: 0.0011 and
// 0.0024, parity with head-on. The bounds sit between the two states.
TEST_CASE("A converged hashed surface stays still at grazing angles", "[hashedalpha][render]")
{
	const float opaqueStill = GrazingFrameDelta(
		"assets/golden/hashed_grazing_still_opaque_a.got.png",
		"assets/golden/hashed_grazing_still_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f,
		c_ConvergeFrames,
		0.0f);
	const float hashedStill = GrazingFrameDelta(
		"assets/golden/hashed_grazing_still_a.got.png",
		"assets/golden/hashed_grazing_still_b.got.png",
		bgl::LayerType::kHashed,
		0.5f,
		c_ConvergeFrames,
		0.0f);
	const float opaquePan = GrazingFrameDelta(
		"assets/golden/hashed_grazing_pan_opaque_a.got.png",
		"assets/golden/hashed_grazing_pan_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f,
		c_GrazePanWarmup,
		c_GrazePanStep);
	const float hashedPan = GrazingFrameDelta(
		"assets/golden/hashed_grazing_pan_a.got.png",
		"assets/golden/hashed_grazing_pan_b.got.png",
		bgl::LayerType::kHashed,
		0.5f,
		c_GrazePanWarmup,
		c_GrazePanStep);

	INFO(
		"grazing flicker: opaqueStill = " << opaqueStill << ", hashedStill = " << hashedStill
										  << ", opaquePan = " << opaquePan
										  << ", hashedPan = " << hashedPan);

	// The instrument has to read zero on a surface that draws the same fragments every frame, or
	// the bounds below measure the harness rather than the hash.
	REQUIRE(opaqueStill < 1e-5f);
	REQUIRE(opaquePan < 1e-5f);

	CHECK(hashedStill < 2.5e-3f);
	CHECK(hashedPan < 5.0e-3f);
}

// The strand regime: geometry whose *texture alpha* is thinner than a pixel, which is the flyaway
// half of a hair asset. Plain box mips dilute such a strand's alpha, and under stochastic coverage
// diluted alpha is a strand that fades out of the frame -- its expected coverage falls with every
// level. The bake's coverage-preserving mips, which kMask always had and kHashed now shares, are
// what keeps the footprint; this is the renderer-level half of that contract, the same strands
// drawn under both mip policies.
//
// What this does *not* claim: that the rescale calms flicker. Measured at parity here (0.0012
// plain, 0.0014 preserved) -- this grating's diluted alpha lands exactly at the matching cutoff,
// where a rescaled texel is still a coin flip per frame. The footprint is the win; the flicker on
// such content is bounded by the accumulation, as elsewhere.
TEST_CASE("Coverage-preserving mips keep hashed strands from dissolving", "[hashedalpha][render]")
{
	const auto [plainDelta, plainMean] = StrandFlicker(
		"assets/golden/hashed_strand_plain_a.got.png",
		"assets/golden/hashed_strand_plain_b.got.png",
		false);

	const auto [preservedDelta, preservedMean] = StrandFlicker(
		"assets/golden/hashed_strand_preserved_a.got.png",
		"assets/golden/hashed_strand_preserved_b.got.png",
		true);

	INFO(
		"strand flicker: plain = " << plainDelta << ", preserved = " << preservedDelta
								   << "; strand luma: plain = " << plainMean.Luma()
								   << ", preserved = " << preservedMean.Luma());

	// The strands have to be on screen at all, or the footprint comparison is background against
	// background.
	CHECK(plainMean.Luma() > 5e-3f);

	// Measured 0.136 against 0.161. Under a resolve that no longer wipes sparse coverage the gap
	// is just the chains' mean-alpha ratio at this minification (1.21 at mip 2.2) -- box mips
	// already preserve a hashed material's expected coverage, and the old half-again gap was the
	// wipe amplifying the difference nonlinearly.
	CHECK(preservedMean.Luma() > plainMean.Luma() * 1.1f);

	// Neither chain may flicker beyond the converged-patch scale; this is where a regression in
	// the accumulation on textured stochastic content would surface.
	CHECK(plainDelta < 3e-3f);
	CHECK(preservedDelta < 3e-3f);
}

// A hash cell is isotropic in world space; the projection is not. Sizing it off the larger screen
// derivative alone leaves it many pixels wide along the compressed axis, so a whole streak of pixels
// shares one threshold -- coverage stops being independent per pixel and becomes a blotch. It reads
// as an occlusion failure rather than as noise, and no amount of temporal accumulation removes it,
// because it does not move.
//
// Blotchiness is the discriminator, not variance: a correlated pattern and a fine one can have the
// same mean and the same spread while differing entirely in how much they change between neighbours.
// That is what AliasEnergy measures.
TEST_CASE("The hashed pattern stays per-pixel at grazing angles", "[hashedalpha][render]")
{
	const std::string grazing = "assets/golden/hashed_alpha_grazing.got.png";
	const std::string flat    = "assets/golden/hashed_alpha_grazing_flat.got.png";

	const bgl::test::Rgba covered = RenderGrazing(flat, bgl::LayerType::kOpaque, 1.0f);
	const bgl::test::Rgba patch   = RenderGrazing(grazing, bgl::LayerType::kHashed, 0.5f);

	// The box has to be on the surface, or everything below measures the background.
	REQUIRE(covered.Luma() > 0.1f);

	const float ratio = patch.Luma() / covered.Luma();
	const float grain = bgl::test::AliasEnergy(grazing, c_GrazeX, c_GrazeY, c_GrazeW, c_GrazeH);

	INFO("grazing: survived = " << ratio << ", grain = " << grain);

	// Correlated cells bias coverage as well as clumping it: sizing the cell off the larger
	// derivative alone measures 0.57 here, and bounding the ratio brings it back to 0.51.
	CHECK(ratio == Catch::Approx(0.5f).margin(0.05));

	// And it has to be per-pixel. Measured: 0.027 with the cell sized off the larger derivative
	// alone, 0.100 with the ratio bounded at 4, and 0.34 bounded at 2. The floor sits between the
	// correlated figure and the bounded ones with room either side.
	CHECK(grain > 0.06f);
}

// The reported artifact, as a number: hair viewed from mid-to-far distance trails a smear when the
// camera pans, and close up it does not. At distance the sampled alpha sits in the open interval
// across the whole tip region, so every pixel there is stochastic coverage -- the neighbourhood
// the resolve clamps against spans strand and backdrop at once, and whatever the box admits
// decays at the blend weight rather than being snapped out. The trailing band is where dragged
// history lands; the leading band is the same stochastic zone on the side being uncovered, so
// blur, jitter and the coverage noise itself cancel out of the comparison.
//
// TAA off is the instrument's zero: no accumulation, so both its bands read the difference between
// two stochastic stills, which is the noise floor the TAA figures are read against.
TEST_CASE("A pan leaves no smear across a hashed alpha ramp", "[hashedalpha][render]")
{
	const SmearBands off = StrandSmear(
		"assets/golden/strand_smear_off_still.got.png",
		"assets/golden/strand_smear_off_panned.got.png",
		false);

	const SmearBands on = StrandSmear(
		"assets/golden/strand_smear_still.got.png",
		"assets/golden/strand_smear_panned.got.png",
		true);

	INFO(
		"band delta against the converged still: taa trail = "
		<< on.trail << ", taa lead = " << on.lead << ", still floor trail = " << on.trailFloor
		<< ", still floor lead = " << on.leadFloor << ", off trail = " << off.trail
		<< ", off lead = " << off.lead);

	// Without TAA the two renders are the same deterministic image, so the zero is exact.
	CHECK(off.trail < 1e-5f);
	CHECK(off.lead < 1e-5f);

	// The still floors are the min/max box's own convergence noise; the moving-box gate reads
	// zero motion at rest, so a tightening that leaks into the resting image shows up here.
	CHECK(on.trailFloor < 5e-4f);
	CHECK(on.leadFloor < 5e-4f);

	// Measured 1.73e-3 with the min/max clamp box and 1.30e-3 with the motion-gated sigma box;
	// the bound sits between them so the wide box cannot return unnoticed. The residual above the
	// floor is reprojection incoherence -- the sprinkle zone's motion alternates between strand
	// and backdrop -- which no clamp box can reach.
	CHECK(on.trail < 1.6e-3f);

	// The leading deficit is convergence lag, larger than the trail and moved less by the box:
	// 2.58e-3 wide, 2.28e-3 tightened. Bounded loosely against gross regression.
	CHECK(on.lead < 3.0e-3f);
}

namespace
{
	// A texture whose every mip is one solid RGBA: whatever colour a draw shows is the mip the
	// shader actually read, and whatever coverage it shows is the alpha it actually sampled.
	assetlib::ImageData
	MakeMipProbeTexture(std::span<const std::array<uint8_t, 4>> mipColors)
	{
		auto image      = assetlib::ImageData();
		image.width     = c_StrandTexSize;
		image.height    = c_StrandTexSize;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_SRGB;
		image.isCubemap = false;
		image.mipLevels = static_cast<uint32_t>(mipColors.size());

		size_t totalBytes = 0;
		for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
		{
			const uint32_t dim = c_StrandTexSize >> mip;
			totalBytes += static_cast<size_t>(dim) * dim * 4;
		}
		image.pixels = core::fixed_buffer<std::byte>(totalBytes);

		size_t offset = 0;
		for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
		{
			const uint32_t dim = c_StrandTexSize >> mip;
			for (size_t t = 0; t < static_cast<size_t>(dim) * dim; ++t)
			{
				std::memcpy(image.pixels.data() + offset + t * 4, mipColors[mip].data(), 4);
			}
			image.subresources.push_back(
				{ offset, static_cast<uint64_t>(dim) * 4, static_cast<uint64_t>(dim) * dim * 4 });
			offset += static_cast<size_t>(dim) * dim * 4;
		}

		return image;
	}

	// One frame of the card, no TAA: the seed is frozen, so the render is deterministic and the
	// patch mean is the coverage times the shaded colour.
	bgl::test::Rgba
	RenderCardMean(const std::string& path, assetlib::ImageData texture)
	{
		PatchScene card = MakeCardScene(std::move(texture), false);
		card.gfx->DrawFrame(card.target, card.job);
		card.gfx->ScreenshotPng(card.target, path);
		return bgl::test::MeanColor(path, c_StrandBoxX, c_StrandBoxY, c_StrandBox, c_StrandBox);
	}
}

// Where the distant-card divergence lives, split in two through the real forward path. The card
// covers about 55 pixels at this camera, 4.7 texels a pixel, so the footprint asks for mip 2.2 of
// its 256-texel chain: a draw showing mip 0's colour means the implicit LOD never left mip 0, and
// a draw showing mip 0's alpha means the chain's alpha never reached the hash. The explicit-LOD
// compute readback of an equivalent chain passes on every backend, so this pins the draw-time
// half alone.
TEST_CASE("A distant card samples the mip its footprint asks for", "[hashedalpha][texture][render]")
{
	// mip 0 red, mip 1 green, mip 2 blue, mip 3 yellow, all opaque.
	constexpr std::array<std::array<uint8_t, 4>, 4> c_ColourPerMip = { {
		{ 255, 0, 0, 255 },
		{ 0, 255, 0, 255 },
		{ 0, 0, 255, 255 },
		{ 255, 255, 0, 255 },
	} };

	// White throughout; only the alpha descends with the chain.
	constexpr std::array<std::array<uint8_t, 4>, 4> c_AlphaPerMip = { {
		{ 255, 255, 255, 255 },
		{ 255, 255, 255, 128 },
		{ 255, 255, 255, 64 },
		{ 255, 255, 255, 32 },
	} };

	constexpr std::array<std::array<uint8_t, 4>, 4> c_WhitePerMip = { {
		{ 255, 255, 255, 255 },
		{ 255, 255, 255, 255 },
		{ 255, 255, 255, 255 },
		{ 255, 255, 255, 255 },
	} };

	const bgl::test::Rgba hue = RenderCardMean(
		"assets/golden/mip_probe_colour.got.png",
		MakeMipProbeTexture(c_ColourPerMip));
	const bgl::test::Rgba faded =
		RenderCardMean("assets/golden/mip_probe_alpha.got.png", MakeMipProbeTexture(c_AlphaPerMip));
	const bgl::test::Rgba full =
		RenderCardMean("assets/golden/mip_probe_full.got.png", MakeMipProbeTexture(c_WhitePerMip));

	// WARN rather than INFO so the figures print on a passing run too: this probe exists to be
	// compared across backends, and the passing side's numbers are half the comparison.
	WARN(
		"hue = " << hue.r << "," << hue.g << "," << hue.b << "  faded luma = " << faded.Luma()
				 << "  full luma = " << full.Luma());

	// The reference has to be lit, or the ratio below compares noise.
	REQUIRE(full.Luma() > 0.05f);

	// Around mip 2 the card reads blue into yellow; at mip 0 it is red and nothing else.
	CHECK(hue.b > hue.r);

	// The sampled alpha around mip 2 is a quarter and below; mip 0's is opaque.
	CHECK(faded.Luma() < full.Luma() * 0.5f);
}

// The user-visible report this instruments: hashed hair thins out and boils as the camera backs
// away, on Metal and D3D12 alike, while the mip probes above clear the texture chain. The ground
// truth is the same card alpha-*blended*: the analytic composite of the very texels, mips and
// lighting the stochastic path samples, so the converged hashed card should match it pixel for
// pixel, and the display curve cancels out of the ratio. `survived` below 1 is coverage the
// resolve destroyed; the frame-to-frame delta is the flicker where it is being rebuilt.
TEST_CASE("A receding hashed card keeps its expected coverage", "[hashedalpha][taa][render]")
{
	struct Rung
	{
		float       cameraZ;
		const char* name;
	};
	constexpr std::array<Rung, 3> c_Rungs = { {
		{ 10.0f, "near" },
		{ 20.0f, "mid" },
		{ 40.0f, "far" },
	} };

	const float tanHalfFov = std::tan(glm::radians(30.0f));

	for (const auto& rung : c_Rungs)
	{
		INFO("distance " << rung.cameraZ);

		const float spanPx =
			c_StrandPlane / (2.0f * rung.cameraZ * tanHalfFov) * static_cast<float>(c_Height);
		const int box   = static_cast<int>(spanPx * 0.6f);
		const int boxXY = (static_cast<int>(c_Width) - box) / 2;

		auto strandImage = MakeStrandTexture(true, true);

		// The chain level the footprint asks for, and the coverage the chain holds there.
		const float    mipF     = std::log2(static_cast<float>(c_StrandTexSize) / spanPx);
		const uint32_t lo       = static_cast<uint32_t>(std::max(0.0f, std::floor(mipF)));
		const uint32_t hi       = std::min(lo + 1, strandImage.mipLevels - 1);
		const float    expected = std::lerp(
			MeanMipAlpha(strandImage, lo),
			MeanMipAlpha(strandImage, hi),
			mipF - std::floor(mipF));

		const auto base = std::string("assets/golden/strand_coverage_") + rung.name;

		PatchScene hashed = MakeCardScene(std::move(strandImage), true, false, rung.cameraZ);
		for (int frame = 0; frame < c_ConvergeFrames; ++frame)
		{
			hashed.gfx->DrawFrame(hashed.target, hashed.job);
		}
		hashed.gfx->ScreenshotPng(hashed.target, base + ".got.png");
		hashed.gfx->DrawFrame(hashed.target, hashed.job);
		hashed.gfx->ScreenshotPng(hashed.target, base + "_next.got.png");

		const float flicker = bgl::test::FrameDelta(
			base + ".got.png",
			base + "_next.got.png",
			boxXY,
			boxXY,
			box,
			box);
		const bgl::test::Rgba strandMean =
			bgl::test::MeanColor(base + ".got.png", boxXY, boxXY, box, box);

		PatchScene blend = MakeCardScene(
			MakeStrandTexture(true, true),
			true,
			false,
			rung.cameraZ,
			bgl::LayerType::kBlend);
		for (int frame = 0; frame < c_ConvergeFrames; ++frame)
		{
			blend.gfx->DrawFrame(blend.target, blend.job);
		}
		blend.gfx->ScreenshotPng(blend.target, base + "_blend.got.png");
		const bgl::test::Rgba blendMean =
			bgl::test::MeanColor(base + "_blend.got.png", boxXY, boxXY, box, box);

		// The reference has to be lit, or the ratio below compares noise.
		REQUIRE(blendMean.Luma() > 0.01f);

		const float survived = strandMean.Luma() / blendMean.Luma();

		// WARN rather than INFO so the figures print on a passing run too: the deficit and its
		// trend with distance are what this instrument exists to show.
		WARN(
			rung.name << ": span " << spanPx << "px  mip " << mipF << "  chain mean alpha "
					  << expected << "  blend luma " << blendMean.Luma() << "  survived "
					  << survived << "  flicker " << flicker);

		// 0.74 near and mid, 1.05 far, measured; the wiping resolve scored 0.23. Below 1 is the
		// minification sharpening trading mid-band energy for far-field crispness -- values under
		// the cutoff are crushed before the step limit re-conserves coverage -- and the bracket
		// guards both directions: hair that vanishes and hair that doubles.
		CHECK(survived > 0.6f);
		CHECK(survived < 1.4f);

		// 1.8e-5 to 4.4e-5 measured -- sharpened alpha leaves few partial values to flip coins
		// with. The wiping resolve's rebuild cycle scored 7.5e-4 to 1.8e-3.
		CHECK(flicker < 2e-4f);
	}
}

// The perceptual half of the distance story, which the coverage ladder above cannot see: a mean
// converged to the blend truth is still a haze the backdrop swallows, while the alpha test draws
// the same content as crisp strokes whose coverage the bake preserves. Sharpening minified alpha
// hands hashed that far-field look, and this pins it: adjacent-pixel contrast of the converged
// far card must sit with the test's figure, not the blend's.
TEST_CASE("Distant hashed strands stay visible features", "[hashedalpha][taa][render]")
{
	constexpr float c_FarZ = 40.0f;

	const float tanHalfFov = std::tan(glm::radians(30.0f));
	const float spanPx =
		c_StrandPlane / (2.0f * c_FarZ * tanHalfFov) * static_cast<float>(c_Height);
	const int box   = static_cast<int>(spanPx * 0.6f);
	const int boxXY = (static_cast<int>(c_Width) - box) / 2;

	const auto energyOf = [&](const char* name, bgl::LayerType layer) {
		PatchScene card = MakeCardScene(MakeStrandTexture(true, true), true, false, c_FarZ, layer);
		for (int frame = 0; frame < c_ConvergeFrames; ++frame)
		{
			card.gfx->DrawFrame(card.target, card.job);
		}
		const auto path = std::string("assets/golden/strand_visibility_") + name + ".got.png";
		card.gfx->ScreenshotPng(card.target, path);
		return bgl::test::AliasEnergy(path, boxXY, boxXY, box, box);
	};

	const float hashed = energyOf("hashed", bgl::LayerType::kHashed);
	const float mask   = energyOf("mask", bgl::LayerType::kMask);
	const float blend  = energyOf("blend", bgl::LayerType::kBlend);

	WARN(
		"far strand contrast: hashed = " << hashed << "  mask = " << mask << "  blend = " << blend);

	// The reference look has to have visible strands at all.
	REQUIRE(mask > 1e-4f);

	CHECK(hashed > blend);
	CHECK(hashed > mask * 0.5f);
}

namespace
{
	// What a distant hair card is at one resolution: how much it flickers, how much strand
	// structure it still has, and how much of it reached the screen at all.
	struct StrandMeasurement
	{
		float flicker = 0.0f;
		float detail  = 0.0f;
		float luma    = 0.0f;

		// Reported because the box shrinks with the frame in the content-fixed sweep: the smallest
		// rung's statistics rest on a few dozen pixels and should be read as such.
		int boxSize = 0;
	};

	StrandMeasurement
	MeasureStrands(
		const Frame&       frame,
		const std::string& name,
		bgl::LayerType     layer = bgl::LayerType::kHashed)
	{
		constexpr float c_CameraZ = 20.0f;

		// The mip policy each layer actually ships with: the bake gives coverage-preserving mips to
		// the two that read alpha as a threshold and plain box mips to blend
		// (`groupPreservesCoverage`). Handing blend the mask's chain would compare it against a
		// texture it never receives.
		const bool preservesCoverage = layer != bgl::LayerType::kBlend;

		PatchScene card = MakeCardScene(
			MakeStrandTexture(preservesCoverage, preservesCoverage),
			true,
			false,
			c_CameraZ,
			layer,
			frame);

		for (int i = 0; i < c_ConvergeFrames; ++i)
		{
			card.gfx->DrawFrame(card.target, card.job);
		}

		const auto pathA = "assets/golden/taa_res_" + name + "_a.got.png";
		const auto pathB = "assets/golden/taa_res_" + name + "_b.got.png";

		card.gfx->ScreenshotPng(card.target, pathA);
		card.gfx->DrawFrame(card.target, card.job);
		card.gfx->ScreenshotPng(card.target, pathB);

		const StrandBox box = StrandBoxFor(frame, c_CameraZ);

		// Below this the box is a handful of pixels and its statistics are noise, not a measurement.
		REQUIRE(box.size >= 8);

		return StrandMeasurement{
			bgl::test::FrameDelta(pathA, pathB, box.xy, box.xy, box.size, box.size),
			bgl::test::AliasEnergy(pathA, box.xy, box.xy, box.size, box.size),
			bgl::test::MeanColor(pathA, box.xy, box.xy, box.size, box.size).Luma(),
			box.size,
		};
	}

	// 256 is the density every other bound in this file was measured at; the rest are its halvings,
	// which is what the editor's render scale and a 1080p display do to a 4K viewport.
	constexpr std::array<uint32_t, 3> c_SweepSizes = { 256, 128, 64 };

	const char*
	LayerName(bgl::LayerType layer)
	{
		switch (layer)
		{
		case bgl::LayerType::kMask:
			return "mask";
		case bgl::LayerType::kBlend:
			return "blend";
		default:
			return "hashed";
		}
	}
}

// The reproduction, as a number. A viewport rendered at half scale puts the same hair card on half
// the pixels, and the report from a 1080p display is that it flickers where a 4K one does not --
// which no bound above can see, because every one of them is measured at 256 alone.
TEST_CASE(
	"A hashed card is measured across render resolutions",
	"[hashedalpha][taa][resolution][render]")
{
	for (const uint32_t size : c_SweepSizes)
	{
		const Frame frame{ size, size, 1.0f };
		const auto  m = MeasureStrands(frame, "content_" + std::to_string(size));

		WARN(
			"content-fixed " << size << "x" << size << ": flicker = " << m.flicker << "  detail = "
							 << m.detail << "  luma = " << m.luma << "  box = " << m.boxSize);
	}
}

// Which of the two mechanisms in the minified path is losing the strands. `kMask` is the alpha test
// whose far-field look SharpenMinifiedAlpha exists to reproduce, and it reads the same
// coverage-preserving mips from the same texture -- so a collapse it shares is the mips or the
// minification itself, and one only hashed suffers is the sharpening. `kBlend` is the energy-true
// reference the sharpening is deliberately not.
TEST_CASE(
	"Each alpha layer is measured across render resolutions",
	"[hashedalpha][taa][resolution][render]")
{
	for (const bgl::LayerType layer :
	     { bgl::LayerType::kMask, bgl::LayerType::kBlend, bgl::LayerType::kHashed })
	{
		for (const uint32_t size : c_SweepSizes)
		{
			const Frame frame{ size, size, 1.0f };
			const auto  name = std::string(LayerName(layer)) + "_" + std::to_string(size);
			const auto  m    = MeasureStrands(frame, "layer_" + name, layer);

			WARN(
				LayerName(layer) << " " << size << "x" << size << ": flicker = " << m.flicker
								 << "  detail = " << m.detail << "  luma = " << m.luma);
		}
	}
}

// The control that says which half is at fault. The field of view narrows with the frame, so a
// strand keeps the pixel footprint it had at 256 and only the frame is smaller: the coverage the
// hash generates is unchanged, and the resolve is the sole thing that varied. Flat here with the
// sweep above rising means the cause is the content going sub-pixel rather than the resolve.
TEST_CASE(
	"A hashed card is measured at a fixed footprint",
	"[hashedalpha][taa][resolution][render]")
{
	for (const uint32_t size : c_SweepSizes)
	{
		const Frame frame{ size, size, static_cast<float>(size) / static_cast<float>(c_Height) };
		const auto  m = MeasureStrands(frame, "footprint_" + std::to_string(size));

		WARN(
			"footprint-fixed " << size << "x" << size << ": flicker = " << m.flicker
							   << "  detail = " << m.detail << "  luma = " << m.luma
							   << "  box = " << m.boxSize);
	}
}
