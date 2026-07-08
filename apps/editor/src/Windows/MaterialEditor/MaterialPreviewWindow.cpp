#include "MaterialPreviewWindow.h"

#include <QDebug>
#include <QResizeEvent>

#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>

namespace
{
	bgl::TextureAssetHandle
	TryLoadTexture(bgl::IScene* scene, const std::string& path)
	{
		if (path.empty())
			return {};
		try
		{
			return scene->AddTextureAsset(assetlib::loadKTX2(path));
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialPreview: failed to load '%s': %s", path.c_str(), e.what());
			return {};
		}
	}
}

MaterialPreviewWindow::MaterialPreviewWindow(
	QWidget*               parent,
	RenderTargetWindowDesc rt,
	MaterialPreviewEnv     env) : RenderTargetWindow(parent, std::move(rt))
{
	bgl::IScene*     scene = PreviewScene();
	bgl::ISceneView* view  = PreviewView();

	// IBL needs all three maps to be valid; the skybox is independent of them.
	const auto irradiance = TryLoadTexture(scene, env.irradiance);
	const auto prefilter  = TryLoadTexture(scene, env.prefilter);
	const auto brdfLut    = TryLoadTexture(scene, env.brdfLut);
	if (irradiance.textureSlot && prefilter.textureSlot && brdfLut.textureSlot)
	{
		try
		{
			view->SetEnvironmentMap({ irradiance, prefilter, brdfLut });
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialPreview: SetEnvironmentMap failed: %s", e.what());
		}
	}

	if (const auto skybox = TryLoadTexture(scene, env.skybox); skybox.textureSlot)
	{
		try
		{
			view->SetSkyBox({ skybox });
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialPreview: SetSkyBox failed: %s", e.what());
		}
	}

	// A neutral default material until the node graph drives one.
	const auto material = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.5f });

	m_Geom         = scene->AddSphereGeom(32, 32, 1.0f, material);
	m_SubmeshCount = 1;
	view->CreateStaticMeshInstance(m_Geom, glm::mat4(1.0f));

	UpdateCamera();
}

void
MaterialPreviewWindow::SetPreviewMaterial(bgl::MaterialHandle material)
{
	if (!m_Geom.IsValid() || !material.IsValid())
		return;

	bgl::IScene* scene = PreviewScene();
	for (uint32_t i = 0; i < m_SubmeshCount; ++i) scene->SetSubmeshMaterial(m_Geom, i, material);
}

void
MaterialPreviewWindow::resizeEvent(QResizeEvent* event)
{
	RenderTargetWindow::resizeEvent(event);
	UpdateCamera();
}

void
MaterialPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	auto cam = bgl::Camera();
	cam.LookAt(glm::vec3(0.0f, 0.0f, 3.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
	SetCamera(cam);
}
