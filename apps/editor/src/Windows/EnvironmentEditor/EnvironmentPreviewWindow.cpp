#include "EnvironmentPreviewWindow.h"

#include "Mesh/BMeshUtil.h"
#include "Mesh/mesh_load.h"
#include "Render/Renderer.h"
#include "util/mime_files.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <assetlib/AssetStore.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RimLightDesc.h>
#include <bgl/SkyboxDesc.h>

namespace
{
	constexpr float c_SphereRadius = 1.0f;

	QString
	FirstMeshUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".bmesh");
	}

	QString
	FirstEnvironmentUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".benv");
	}
}

EnvironmentPreviewWindow::EnvironmentPreviewWindow(QWidget* parent, RenderTargetWindowDesc rt) :
	RenderTargetWindow(parent, std::move(rt))
{
	setAcceptDrops(true);
	setFocusPolicy(Qt::StrongFocus);

	m_Material = GetRenderer()->Invoke([&] {
		// Mid grey and fairly rough: a mirror would show the environment instead of being lit by
		// it, and the rim is what this viewport is for.
		return GetPreviewScene()->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 0.7f });
	});

	ShowSphere();
}

void
EnvironmentPreviewWindow::Release()
{
	ClearGeometry();

	GetRenderer()->Invoke([&] {
		bgl::IScene* scene = GetPreviewScene();

		for (const bgl::TextureAssetHandle* map : { &m_Environment.bound.irradiance,
		                                            &m_Environment.bound.prefilter,
		                                            &m_Environment.bound.skybox })
		{
			if (map->textureSlot)
				scene->DeleteTextureAsset(*map);
		}
		m_Environment.bound = {};

		if (m_Material.IsValid())
			scene->DeleteMaterial(m_Material);
		m_Material = {};
	});
}

void
EnvironmentPreviewWindow::Bind(const std::string& benvPath, const std::filesystem::path& dataRoot)
{
	m_DataRoot = dataRoot;

	// What an unset override falls back to. The resolve folds the two together, so it cannot say
	// which of them it used -- and this viewport has to show both.
	m_DerivedExposure = 1.0f;
	try
	{
		const assetlib::AssetStore store(dataRoot);
		const assetlib::BEnv       env = store.Load<assetlib::BEnv>(store.KeyFor(benvPath));
		if (!env.pbr.lighting.empty())
			m_DerivedExposure = store.Load<assetlib::BEnvLighting>(env.pbr.lighting).exposure;
	}
	catch (const std::exception& e)
	{
		qWarning("EnvironmentPreview: cannot read '%s': %s", benvPath.c_str(), e.what());
	}

	GetRenderer()->Invoke([&] {
		editor::BindEnvironment(
			GetPreviewScene(),
			GetPreviewView(),
			m_Environment,
			benvPath,
			dataRoot,
			"EnvironmentPreview");
	});
}

void
EnvironmentPreviewWindow::ApplyEdits(const assetlib::BEnv& env)
{
	GetRenderer()->Invoke([&] {
		bgl::ISceneView* view = GetPreviewView();

		view->SetRimLight(
			{ .tint = env.rim.tint, .intensity = env.rim.intensity, .power = env.rim.power });

		view->SetExposure(env.pbr.exposureOverride.value_or(m_DerivedExposure));

		if (m_Environment.bound.skybox.textureSlot)
		{
			view->SetSkyBox(
				{ m_Environment.bound.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
		}
	});
}

void
EnvironmentPreviewWindow::ShowSphere()
{
	ClearGeometry();

	const bgl::GeomHandle sphere = GetRenderer()->Invoke(
		[&] { return GetPreviewScene()->AddSphereGeom(48, 48, c_SphereRadius, m_Material); });

	Place(sphere, glm::mat4(1.0f));

	m_Camera.FocusOn(glm::vec3(0.0f), c_SphereRadius, 0.0f, 0.2f);
	PushCamera();
}

void
EnvironmentPreviewWindow::ShowMesh(const std::filesystem::path& path)
{
	assetlib::BMesh mesh;
	try
	{
		mesh = editor::LoadMeshThroughSeam(m_DataRoot, path);
	}
	catch (const std::exception& e)
	{
		qWarning("EnvironmentPreview: cannot load '%s': %s", path.string().c_str(), e.what());
		return;
	}

	ClearGeometry();

	auto boundsMin = glm::vec3(std::numeric_limits<float>::max());
	auto boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

	// The preview's own material on every submesh, deliberately: what is being judged is the
	// environment, and a mesh's own textures would be the loudest thing on screen.
	const auto materials = std::vector<bgl::MaterialHandle>(mesh.materials.size(), m_Material);

	for (const bmesh::InstancePlacement& placement : bmesh::PlanInstances(mesh))
	{
		const bgl::GeomHandle geom = GetRenderer()->Invoke([&] {
			return GetPreviewScene()->AddStaticMeshGeom(mesh, placement.meshIndex, materials);
		});

		Place(geom, placement.world);
		bmesh::GrowBoundsForMesh(mesh, placement.meshIndex, placement.world, boundsMin, boundsMax);
	}

	if (m_Instances.empty())
		return;

	const glm::vec3 centre = (boundsMin + boundsMax) * 0.5f;
	m_Camera.FocusOn(centre, glm::length(boundsMax - boundsMin) * 0.5f, 0.0f, 0.2f);
	PushCamera();
}

void
EnvironmentPreviewWindow::Place(bgl::GeomHandle geom, const glm::mat4& transform)
{
	if (!geom.IsValid())
		return;

	GetRenderer()->Invoke([&] {
		const bgl::MeshInstanceHandle instance =
			GetPreviewView()->CreateStaticMeshInstance(geom, transform);

		// A full share whatever the mesh's own default asked for: see the class comment.
		GetPreviewView()->SetInstanceRimIntensity(instance, 1.0f);

		m_Instances.push_back(instance);
		m_Geoms.push_back(geom);
	});
}

void
EnvironmentPreviewWindow::ClearGeometry()
{
	GetRenderer()->Invoke([&] {
		for (const bgl::MeshInstanceHandle& instance : m_Instances)
		{
			try
			{
				GetPreviewView()->DeleteMeshInstance(instance);
			}
			catch (const std::exception& e)
			{
				qWarning("EnvironmentPreview: failed to delete an instance: %s", e.what());
			}
		}

		for (const bgl::GeomHandle& geom : m_Geoms)
		{
			try
			{
				GetPreviewScene()->DeleteGeom(geom);
			}
			catch (const std::exception& e)
			{
				qWarning("EnvironmentPreview: failed to delete a geom: %s", e.what());
			}
		}
	});

	m_Instances.clear();
	m_Geoms.clear();
}

void
EnvironmentPreviewWindow::PushCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	SetCamera(m_Camera.GetCamera(aspect));
}

void
EnvironmentPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (!FirstMeshUrl(event->mimeData()).isEmpty() ||
	    !FirstEnvironmentUrl(event->mimeData()).isEmpty())
	{
		event->acceptProposedAction();
	}
}

void
EnvironmentPreviewWindow::dropEvent(QDropEvent* event)
{
	if (const QString benv = FirstEnvironmentUrl(event->mimeData()); !benv.isEmpty())
	{
		event->acceptProposedAction();
		Q_EMIT EnvironmentDropped(benv);
		return;
	}

	if (const QString mesh = FirstMeshUrl(event->mimeData()); !mesh.isEmpty())
	{
		event->acceptProposedAction();
		ShowMesh(std::filesystem::path(mesh.toStdWString()));
	}
}

void
EnvironmentPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_LastMouse = event->position().toPoint();
}

void
EnvironmentPreviewWindow::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos   = event->position().toPoint();
	const QPoint delta = pos - m_LastMouse;
	m_LastMouse        = pos;

	if (event->buttons() & Qt::LeftButton)
		m_Camera.Orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	else if (event->buttons() & (Qt::MiddleButton | Qt::RightButton))
		m_Camera.Pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	else
		return;

	PushCamera();
}

void
EnvironmentPreviewWindow::wheelEvent(QWheelEvent* event)
{
	m_Camera.Dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
	PushCamera();
	event->accept();
}

void
EnvironmentPreviewWindow::resizeEvent(QResizeEvent* event)
{
	RenderTargetWindow::resizeEvent(event);
	PushCamera();
}
