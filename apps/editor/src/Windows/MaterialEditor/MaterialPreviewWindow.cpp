#include "MaterialPreviewWindow.h"

#include "Async/BackgroundTask.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QUrl>
#include <QWheelEvent>

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

	// A node's transform composed with all of its ancestors'.
	glm::mat4
	WorldTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex)
	{
		auto     world = glm::mat4(1.0f);
		uint32_t index = nodeIndex;
		while (index != assetlib::c_InvalidIndex && index < mesh.nodes.size())
		{
			const assetlib::Node& node = mesh.nodes[index];
			world                      = assetlib::toMatrix(node.localTransform) * world;
			index                      = node.parent;
		}
		return world;
	}

	// Grows [outMin,outMax] to contain the submesh's box after `transform`, corner by corner (the
	// box is not axis-aligned once rotated).
	void
	GrowBounds(
		const glm::mat4& transform,
		const glm::vec3& boxMin,
		const glm::vec3& boxMax,
		glm::vec3&       outMin,
		glm::vec3&       outMax)
	{
		for (int corner = 0; corner < 8; ++corner)
		{
			const auto point = glm::vec3(
				(corner & 1) ? boxMax.x : boxMin.x,
				(corner & 2) ? boxMax.y : boxMin.y,
				(corner & 4) ? boxMax.z : boxMin.z);

			const auto world = glm::vec3(transform * glm::vec4(point, 1.0f));
			outMin           = glm::min(outMin, world);
			outMax           = glm::max(outMax, world);
		}
	}

	QString
	ResolveMaterialPath(
		const assetlib::BMesh&       mesh,
		const assetlib::Submesh&     submesh,
		const std::filesystem::path& dataRoot)
	{
		if (dataRoot.empty() || submesh.material >= mesh.materials.size())
			return {};

		const std::string& relative = mesh.materials[submesh.material];
		if (relative.empty())
			return {};

		const auto resolved = (dataRoot / relative).lexically_normal();
		return QString::fromStdWString(resolved.wstring());
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

	// Wheel events only reach a widget that can take focus, and the camera needs them to dolly.
	setFocusPolicy(Qt::StrongFocus);

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
		// Instances first: a geom cannot be deleted while an instance still references it.
		for (const bgl::MeshInstanceHandle& instance : m_Instances)
		{
			if (instance.IsValid())
				PreviewView()->DeleteMeshInstance(instance);
		}
		for (const bgl::GeomHandle& geom : m_Geoms)
		{
			if (geom.IsValid())
				PreviewScene()->DeleteGeom(geom);
		}
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: failed to clear geometry: %s", e.what());
	}

	m_Instances.clear();
	m_Geoms.clear();
	m_SubmeshRefs.clear();
	m_SubmeshNames.clear();
	m_SubmeshMaterialPaths.clear();
	m_MeshPath.clear();  // LoadMesh sets it again once it has succeeded
}

uint32_t
MaterialPreviewWindow::SourceSubmesh(uint32_t submeshIndex) const noexcept
{
	if (m_MeshPath.empty() || submeshIndex >= m_SubmeshRefs.size())
		return assetlib::c_InvalidIndex;
	return m_SubmeshRefs[submeshIndex].sourceSubmesh;
}

void
MaterialPreviewWindow::ShowDefaultSphere()
{
	ClearGeometry();

	m_Geoms.push_back(PreviewScene()->AddSphereGeom(32, 32, 1.0f, m_DefaultMaterial));
	m_Instances.push_back(PreviewView()->CreateStaticMeshInstance(m_Geoms.back(), glm::mat4(1.0f)));

	m_SubmeshRefs.push_back({ 0, 0, 0 });
	m_SubmeshNames = QStringList{ "Sphere" };  // procedural sphere: a single submesh

	m_SubmeshMaterialPaths = QStringList{ QString() };
	FocusOn(glm::vec3(0.0f), 1.0f);

	Q_EMIT GeometryChanged();
}

void
MaterialPreviewWindow::LoadMesh(const std::filesystem::path& path)
{
	// Deserializing the .bmesh is the slow half and touches no bgl state, so it runs on
	// a worker. Everything below mutates the Scene and the SceneView, which carry no locks, so it
	// has to wait until the worker is done.
	assetlib::BMesh mesh;
	QString         readError;

	const bool read = background::RunWithLoadingScreen(
		this,
		QString("Loading %1").arg(QString::fromStdString(path.filename().string())),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Reading mesh...");
			mesh = assetlib::load(path);
			if (mesh.meshes.empty())
				throw std::runtime_error("mesh contains no meshes");
		},
		&readError);

	if (!read)
	{
		qWarning(
			"MaterialPreview: failed to load mesh '%s': %s",
			path.string().c_str(),
			qPrintable(readError));
		ShowDefaultSphere();
		return;
	}

	try
	{
		ClearGeometry();

		bgl::IScene* scene = PreviewScene();

		// The preview authors a material, so bind the same neutral material to every source material
		// slot; the graph then rebinds it per submesh.
		const auto materials = std::vector<bgl::MaterialHandle>(
			std::max<size_t>(1, mesh.materials.size()),
			m_DefaultMaterial);

		// A .bmesh spreads its submeshes across several meshes, and a node instances a mesh (the same
		// mesh can be instanced by several nodes). Upload each mesh once, then place an instance for
		// every node that references one, at that node's world transform.
		auto geomForMesh = std::unordered_map<uint32_t, uint32_t>();  // mesh index -> m_Geoms index
		auto aabbMin     = glm::vec3(std::numeric_limits<float>::max());
		auto aabbMax     = glm::vec3(std::numeric_limits<float>::lowest());

		for (uint32_t nodeIndex = 0; nodeIndex < mesh.nodes.size(); ++nodeIndex)
		{
			const assetlib::Node& node = mesh.nodes[nodeIndex];
			if (node.mesh == assetlib::c_InvalidIndex || node.mesh >= mesh.meshes.size())
				continue;

			auto [it, inserted] =
				geomForMesh.try_emplace(node.mesh, static_cast<uint32_t>(m_Geoms.size()));
			if (inserted)
			{
				m_Geoms.push_back(scene->AddStaticMesh(mesh, node.mesh, materials));

				// Name each of this mesh's submeshes once, in the order the selector will show them.
				const assetlib::Mesh& entry = mesh.meshes[node.mesh];
				for (uint32_t i = 0; i < entry.submeshCount; ++i)
				{
					const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];

					auto name =
						QString::fromStdString(NameFromPool(mesh.stringPool, submesh.nameOffset));
					if (name.isEmpty())
						name = QString("Submesh %1").arg(m_SubmeshNames.size());
					m_SubmeshNames << name;
					m_SubmeshMaterialPaths << ResolveMaterialPath(mesh, submesh, m_DataRoot);
					m_SubmeshRefs.push_back({ it->second, i, entry.firstSubmesh + i });
				}
			}

			const glm::mat4 world = WorldTransform(mesh, nodeIndex);
			m_Instances.push_back(
				PreviewView()->CreateStaticMeshInstance(m_Geoms[it->second], world));

			const assetlib::Mesh& entry = mesh.meshes[node.mesh];
			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];
				GrowBounds(world, submesh.aabbMin, submesh.aabbMax, aabbMin, aabbMax);
			}
		}

		if (m_Geoms.empty())
			throw std::runtime_error("no node references a mesh");

		const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
		const float     radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);
		FocusOn(center, radius);

		m_MeshPath = path;

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
	if (!material.IsValid() || submeshIndex >= m_SubmeshRefs.size())
		return;

	const SubmeshRef& ref = m_SubmeshRefs[submeshIndex];
	if (ref.geomIndex >= m_Geoms.size() || !m_Geoms[ref.geomIndex].IsValid())
		return;

	try
	{
		PreviewScene()->SetSubmeshMaterial(m_Geoms[ref.geomIndex], ref.localSubmesh, material);
	}
	catch (const std::exception& e)
	{
		// A source submesh can split into several GPU submeshes when it exceeds the meshlet cap, so
		// the source submesh index is not always a valid GPU submesh index.
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

	// Pull back far enough that the focus sphere fits the field of view, with some margin.
	m_Distance = radius * 3.0f;
	m_Yaw      = 0.0f;
	m_Pitch    = 0.0f;

	UpdateCamera();
}

void
MaterialPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_DragButton   = event->button();
	m_LastMousePos = event->position().toPoint();
}

void
MaterialPreviewWindow::mouseReleaseEvent(QMouseEvent*)
{
	m_DragButton = Qt::NoButton;
}

void
MaterialPreviewWindow::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos   = event->position().toPoint();
	const QPoint delta = pos - m_LastMousePos;
	m_LastMousePos     = pos;

	if (m_DragButton == Qt::LeftButton)
	{
		// Orbit. Clamp the pitch just short of the poles so the view never flips over.
		constexpr float c_RadiansPerPixel = 0.01f;
		constexpr float c_PitchLimit      = 1.55f;  // just under 90 degrees

		m_Yaw -= static_cast<float>(delta.x()) * c_RadiansPerPixel;
		m_Pitch = std::clamp(
			m_Pitch + static_cast<float>(delta.y()) * c_RadiansPerPixel,
			-c_PitchLimit,
			c_PitchLimit);
	}
	else if (m_DragButton == Qt::MiddleButton || m_DragButton == Qt::RightButton)
	{
		// Pan: slide the focus point across the view plane, scaled so the model tracks the cursor.
		const glm::vec3 forward = glm::normalize(m_FocusCenter - EyePosition());
		const glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		const glm::vec3 up      = glm::cross(right, forward);

		const float scale = m_Distance * 0.002f;
		m_FocusCenter += right * (-static_cast<float>(delta.x()) * scale);
		m_FocusCenter += up * (static_cast<float>(delta.y()) * scale);
	}
	else
	{
		return;
	}

	UpdateCamera();
}

void
MaterialPreviewWindow::wheelEvent(QWheelEvent* event)
{
	// Dolly geometrically so each notch feels the same at any distance.
	const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
	m_Distance =
		std::clamp(m_Distance * std::pow(0.9f, steps), m_FocusRadius * 0.1f, m_FocusRadius * 50.0f);

	UpdateCamera();
	event->accept();
}

glm::vec3
MaterialPreviewWindow::EyePosition() const
{
	const glm::vec3 direction(
		std::cos(m_Pitch) * std::sin(m_Yaw),
		std::sin(m_Pitch),
		std::cos(m_Pitch) * std::cos(m_Yaw));
	return m_FocusCenter + direction * m_Distance;
}

void
MaterialPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	const glm::vec3 eye = EyePosition();

	auto cam = bgl::Camera();
	cam.LookAt(eye, m_FocusCenter, glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(45.0f),
			aspect,
			std::max(0.001f, m_FocusRadius * 0.01f),
			m_Distance + m_FocusRadius * 50.0f);
	SetCamera(cam);
}
