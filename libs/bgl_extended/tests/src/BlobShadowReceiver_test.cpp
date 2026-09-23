#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/InstanceDesc.h>
#include <bgl/MaterialHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/RigHandle.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

/**
 * Units are no receivers: a skinned placement passing beneath a caster keeps its own colour,
 * while the ground around it takes the shadow.
 *
 * The unit is a flat, upward-facing quad rigidly skinned to a one-bone rig and held a quarter of
 * a unit under a static caster -- exactly the surface the decal would darken hardest were it read
 * as a receiver: facing up, well inside the fade height, in the disc's core.
 */

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	constexpr float c_UnitHeight = 0.25f;
	constexpr float c_UnitHalf   = 0.4f;
	constexpr float c_UnitX      = 1.2f;

	const glm::mat4 c_Flat =
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	assetlib::Skeleton
	MakeOneBoneRig()
	{
		auto bone        = assetlib::Bone();
		bone.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		bone.inverseBind = glm::mat4(1.0f);
		bone.parent      = assetlib::c_InvalidIndex;

		auto skeleton   = assetlib::Skeleton();
		bone.nameOffset = skeleton.stringPool.add("Root");
		skeleton.bones.push_back(bone);
		return skeleton;
	}

	assetlib::AnimationSet
	MakeStill()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = 1;
		for (uint32_t f = 0; f < 2; ++f)
		{
			set.samples.push_back(
				{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
		}

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = 2;
		clip.sampleRate  = 30.0f;
		clip.duration    = 1.0f / 30.0f;
		clip.loop        = 0;
		clip.nameOffset  = 0;
		set.clips.push_back(clip);
		return set;
	}

	/** A quad in XZ, normal +Y, every vertex wholly bound to bone 0. */
	assetlib::BMesh
	MakeSkinnedQuad()
	{
		constexpr uint16_t c_Stride = 12 + 12 + 8 + 8;

		const std::array<glm::vec3, 4> corners = { {
			glm::vec3(-c_UnitHalf, 0.0f, -c_UnitHalf),
			glm::vec3(-c_UnitHalf, 0.0f, c_UnitHalf),
			glm::vec3(c_UnitHalf, 0.0f, c_UnitHalf),
			glm::vec3(c_UnitHalf, 0.0f, -c_UnitHalf),
		} };

		auto mesh = assetlib::BMesh();
		mesh.vertexData.resize(corners.size() * c_Stride);
		for (size_t v = 0; v < corners.size(); ++v)
		{
			std::byte*                    vertex  = mesh.vertexData.data() + v * c_Stride;
			const glm::vec3               normal  = glm::vec3(0.0f, 1.0f, 0.0f);
			const std::array<uint16_t, 4> joints  = { 0, 0, 0, 0 };
			const std::array<uint16_t, 4> weights = { 0xFFFF, 0, 0, 0 };
			std::memcpy(vertex, &corners[v], 12);
			std::memcpy(vertex + 12, &normal, 12);
			std::memcpy(vertex + 24, joints.data(), 8);
			std::memcpy(vertex + 32, weights.data(), 8);
		}

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingRadius = 4.0f;
		mesh.meshlets.push_back(meshlet);

		for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);
		constexpr std::array<uint8_t, 6> c_Indices = { 0, 1, 2, 0, 2, 3 };
		for (const uint8_t index : c_Indices) mesh.meshletTriangles.push_back(index);

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 4;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                              assetlib::VertexFormat::kFloat32x3,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              24 };
		submesh.layout.attributes[3]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              32 };
		submesh.vertexCount           = 4;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(-c_UnitHalf, 0.0f, -c_UnitHalf);
		submesh.aabbMax               = glm::vec3(c_UnitHalf, 0.0f, c_UnitHalf);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}
}

TEST_CASE(
	"A unit passing beneath a caster takes none of its shadow",
	"[blobshadow][skinned][render]")
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
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 16;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 8192;
	sceneDesc.initialIndices              = 256;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	scene->SetGround(bgl::GroundPlaneDesc());
	auto view = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto whiteDesc            = bgl::PbrMaterialDesc();
	whiteDesc.baseColorFactor = glm::vec4(1.0f);
	whiteDesc.metallicFactor  = 0.0f;
	whiteDesc.roughnessFactor = 1.0f;
	const auto white          = scene->CreatePbrMaterial(whiteDesc);

	const auto groundGeom = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, white);
	const auto casterGeom = scene->AddPlaneGeom(1, 1, 0.5f, 0.5f, white);
	view->CreateStaticMeshInstance(groundGeom, c_Flat);
	const auto caster = view->CreateStaticMeshInstance(
		casterGeom,
		glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)) * c_Flat);

	const std::array<bgl::MaterialHandle, 1> materials = { { white } };

	const bgl::RigHandle rig = scene->AddRig(MakeOneBoneRig(), MakeStill());
	REQUIRE(rig.IsValid());
	const bgl::GeomHandle unitGeom = scene->AddSkinnedMeshGeom(
		MakeSkinnedQuad(),
		0,
		materials,
		rig,
		assetlib::Bounds{ glm::vec3(-1.0f), glm::vec3(1.0f) });
	REQUIRE(unitGeom.IsValid());
	view->CreateSkinnedMeshInstance(
		unitGeom,
		glm::translate(glm::mat4(1.0f), glm::vec3(c_UnitX, c_UnitHeight, 0.0f)),
		bgl::SkinnedInstanceDesc{ 0, 0.0f, 0.0f });

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 8.0f, 14.0f),
			glm::vec3(0.0f, 0.0f, 0.0f),
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

	constexpr int c_Box = 8;
	const auto    boxAt = [&](const glm::vec3& world) {
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(world, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return glm::ivec2(
			static_cast<int>((ndc.x * 0.5f + 0.5f) * c_Width) - c_Box / 2,
			static_cast<int>((0.5f - ndc.y * 0.5f) * c_Height) - c_Box / 2);
	};

	// The unit's centre, and the ground mirrored across the caster, the same distance from it.
	const glm::ivec2 unitBox   = boxAt(glm::vec3(c_UnitX, c_UnitHeight, 0.0f));
	const glm::ivec2 groundBox = boxAt(glm::vec3(-c_UnitX, 0.0f, 0.0f));

	struct Lumas
	{
		float unit   = 0.0f;
		float ground = 0.0f;
	};
	const auto sample = [&](const char* name) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		const auto lumas = Lumas{
			bgl::test::MeanColor(path, unitBox.x, unitBox.y, c_Box, c_Box).Luma(),
			bgl::test::MeanColor(path, groundBox.x, groundBox.y, c_Box, c_Box).Luma(),
		};
		std::filesystem::remove(path);
		return lumas;
	};

	const Lumas base = sample("bernini_blob_unit_base");
	REQUIRE(base.unit > 0.05f);
	REQUIRE(base.ground > 0.05f);

	auto desc       = bgl::BlobShadowDesc();
	desc.radius     = 5.0f;
	desc.intensity  = 0.9f;
	desc.fadeHeight = 2.0f;
	view->SetBlobShadow(caster, desc);

	const Lumas shadowed = sample("bernini_blob_unit_shadowed");

	// The disc reaches this far out: the ground at the unit's distance darkens.
	CHECK(shadowed.ground < base.ground * 0.9f);

	// And the unit, standing in it, is untouched.
	CHECK(shadowed.unit > base.unit * 0.99f);
	CHECK(shadowed.unit < base.unit * 1.01f);
}
