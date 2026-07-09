#include "MaterialPreviewWindow.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QUrl>

#include <assetlib/bmesh_io.h>
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>

namespace
{
	// Loads a KTX2 into the scene as a texture asset; on failure returns an invalid handle (and logs)
	// so a missing/optional environment asset degrades gracefully instead of throwing.
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

	// The NUL-terminated name at `offset` in a BMesh's string pool (empty for offset 0 / out of range).
	std::string
	NameFromPool(const std::vector<char>& pool, uint32_t offset)
	{
		if (offset == 0 || offset >= pool.size())
			return {};
		return std::string(pool.data() + offset);
	}

	bool
	IsPreviewMesh(const QString& localFile)
	{
		return localFile.endsWith(".bmesh", Qt::CaseInsensitive);
	}

	// The first local `.bmesh` in a drag's payload, or an empty string.
	QString
	FirstMeshUrl(const QMimeData* mime)
	{
		if (mime == nullptr || !mime->hasUrls())
			return {};
		for (const QUrl& url : mime->urls())
		{
			if (url.isLocalFile() && IsPreviewMesh(url.toLocalFile()))
				return url.toLocalFile();
		}
		return {};
	}
}

MaterialPreviewWindow::MaterialPreviewWindow(
	QWidget*               parent,
	RenderTargetWindowDesc rt,
	MaterialPreviewEnv     env) : RenderTargetWindow(parent, std::move(rt))
{
	bgl::IScene*     scene = PreviewScene();
	bgl::ISceneView* view  = PreviewView();

	setAcceptDrops(true);

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

	// One neutral material, reused for every geometry swap, until the node graph drives one.
	m_DefaultMaterial = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = 0.1f, .roughnessFactor = 0.5f });

	ShowDefaultSphere();
}

void
MaterialPreviewWindow::ClearGeometry()
{
	try
	{
		if (m_Instance.IsValid())
			PreviewView()->DeleteMeshInstance(m_Instance);
		if (m_Geom.IsValid())
			PreviewScene()->DeleteGeom(m_Geom);
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: failed to clear geometry: %s", e.what());
	}

	m_Instance = {};
	m_Geom     = {};
	m_SubmeshNames.clear();
}

void
MaterialPreviewWindow::ShowDefaultSphere()
{
	ClearGeometry();

	m_Geom     = PreviewScene()->AddSphereGeom(32, 32, 1.0f, m_DefaultMaterial);
	m_Instance = PreviewView()->CreateStaticMeshInstance(m_Geom, glm::mat4(1.0f));

	m_SubmeshNames = QStringList{ "Sphere" };  // procedural sphere: a single submesh
	FocusOn(glm::vec3(0.0f), 1.0f);

	Q_EMIT GeometryChanged();
}

void
MaterialPreviewWindow::LoadMesh(const std::filesystem::path& path)
{
	try
	{
		const auto mesh = assetlib::load(path);
		if (mesh.meshes.empty())
			throw std::runtime_error("mesh contains no meshes");

		ClearGeometry();

		bgl::IScene* scene = PreviewScene();

		// The preview authors a material, so bind the same neutral material to every source material
		// slot; the graph then rebinds it per submesh.
		const auto materials = std::vector<bgl::MaterialHandle>(
			std::max<size_t>(1, mesh.materials.size()),
			m_DefaultMaterial);

		m_Geom     = scene->AddStaticMesh(mesh, 0, materials);
		m_Instance = PreviewView()->CreateStaticMeshInstance(m_Geom, glm::mat4(1.0f));

		// Submesh names come from the source mesh's string pool (see Submesh::nameOffset).
		const assetlib::Mesh& entry   = mesh.meshes[0];
		auto                  aabbMin = glm::vec3(std::numeric_limits<float>::max());
		auto                  aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
		for (uint32_t i = 0; i < entry.submeshCount; ++i)
		{
			const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];

			auto name = QString::fromStdString(NameFromPool(mesh.stringPool, submesh.nameOffset));
			if (name.isEmpty())
				name = QString("Submesh %1").arg(i);
			m_SubmeshNames << name;

			aabbMin = glm::min(aabbMin, submesh.aabbMin);
			aabbMax = glm::max(aabbMax, submesh.aabbMax);
		}

		const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
		const float     radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);
		FocusOn(center, radius);

		Q_EMIT GeometryChanged();
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: failed to load mesh '%s': %s", path.string().c_str(), e.what());
		ShowDefaultSphere();
	}
}

void
MaterialPreviewWindow::SetSubmeshMaterial(uint32_t submeshIndex, bgl::MaterialHandle material)
{
	if (!m_Geom.IsValid() || !material.IsValid())
		return;

	try
	{
		PreviewScene()->SetSubmeshMaterial(m_Geom, submeshIndex, material);
	}
	catch (const std::exception& e)
	{
		// A source submesh can split into several GPU submeshes when it exceeds the meshlet cap, so
		// the selector's index is not always a valid GPU submesh index.
		qWarning("MaterialPreview: SetSubmeshMaterial(%u) failed: %s", submeshIndex, e.what());
	}
}

void
MaterialPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (!FirstMeshUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (!FirstMeshUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::dropEvent(QDropEvent* event)
{
	const QString file = FirstMeshUrl(event->mimeData());
	if (file.isEmpty())
		return;

	LoadMesh(std::filesystem::path(file.toStdWString()));
	event->acceptProposedAction();
}

void
MaterialPreviewWindow::resizeEvent(QResizeEvent* event)
{
	RenderTargetWindow::resizeEvent(event);
	UpdateCamera();
}

void
MaterialPreviewWindow::FocusOn(const glm::vec3& center, float radius)
{
	m_FocusCenter = center;
	m_FocusRadius = radius;
	UpdateCamera();
}

void
MaterialPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	// Pull back far enough that the focus sphere fits the vertical field of view, with some margin.
	const float     distance = m_FocusRadius * 3.0f;
	const glm::vec3 eye      = m_FocusCenter + glm::vec3(0.0f, 0.0f, distance);

	auto cam = bgl::Camera();
	cam.LookAt(eye, m_FocusCenter, glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(45.0f),
			aspect,
			std::max(0.001f, m_FocusRadius * 0.01f),
			distance + m_FocusRadius * 10.0f);
	SetCamera(cam);
}
