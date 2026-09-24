#include "MaterialPreviewWindow.h"

#include "mesh_drop_import.h"
#include <QEvent>
#include <QVBoxLayout>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_sdk/mesh_load.h>

#include <editor_plugin_api/IEditorHost.h>
#include <editor_sdk/environment.h>

#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <bgl/GeomHandle.h>
#include <editor_sdk/BMeshUtil.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/mesh_drop.h>
#include <editor_sdk/mime_files.h>
#include <gamelib/Ray.h>

#include <QApplication>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMesh.h>
#include <bgl/Camera.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/localize.h>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
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

	QString
	FirstEnvironmentUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".benv");
	}
}

MaterialPreviewWindow::MaterialPreviewWindow(
	editor::IEditorHost& host,
	QWidget*             parent,
	editor::ViewportDesc rt,
	MaterialPreviewEnv   env) :
	QWidget(parent), m_Host(host), m_Viewport(host.CreateViewport(this, rt))
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_Viewport);
	m_Viewport->setAcceptDrops(true);
	m_Viewport->installEventFilter(this);
	m_DataRoot = host.GetStore().GetDataRoot();
	setAcceptDrops(true);

	// Wheel events only reach a widget that can take focus, and the camera needs them to dolly.
	setFocusPolicy(Qt::StrongFocus);

	m_Environment.configured = std::move(env);

	BindConfiguredEnvironment();

	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		bgl::IScene* scene = &context.scene;

		m_DefaultMaterial = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
	});

	ShowDefaultSphere();
}

MaterialPreviewWindow::~MaterialPreviewWindow()
{
	SetRenderingEnabled(false);
	ClearGeometry();
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		try
		{
			editor::ReleaseEnvironment(&context.scene, m_Environment);
			if (m_DefaultMaterial.IsValid())
				context.scene.DeleteMaterial(m_DefaultMaterial);
		}
		catch (const std::exception& error)
		{
			qWarning("MaterialPreview: failed to release preview resources: %s", error.what());
		}
	});
}

void
MaterialPreviewWindow::ClearGeometry()
{
	// Before anything is released, so a listener still sees the mesh these submeshes belong to.
	if (!m_MeshPath.empty())
		Q_EMIT GeometryAboutToChange();

	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		for (const InstanceRef& instance : m_Instances)
		{
			if (!instance.handle.IsValid())
				continue;

			try
			{
				view->DeleteMeshInstance(instance.handle);
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialPreview: failed to delete an instance: %s", e.what());
			}
		}

		for (const bgl::GeomHandle& geom : m_Geoms)
		{
			if (!geom.IsValid())
				continue;

			try
			{
				context.scene.DeleteGeom(geom);
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialPreview: failed to delete a geom: %s", e.what());
			}
		}
	});

	m_Raycaster.Clear();
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

bool
MaterialPreviewWindow::SubmeshHasTangent(uint32_t submeshIndex) const noexcept
{
	return submeshIndex >= m_SubmeshRefs.size() || m_SubmeshRefs[submeshIndex].hasTangent;
}

void
MaterialPreviewWindow::ShowDefaultSphere()
{
	ClearGeometry();

	try
	{
		m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
			m_Geoms.push_back(context.scene.AddSphereGeom(32, 32, 1.0f, m_DefaultMaterial));
			m_Instances.push_back(
				{ view->CreateStaticMeshInstance(m_Geoms.back(), glm::mat4(1.0f)), 0 });

			// The sphere's triangles never exist on the CPU, so its raycast shadow is analytic.
			m_Raycaster.AddInstance(m_Raycaster.AddSphere(1.0f), glm::mat4(1.0f));
		});
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: could not show the default sphere: %s", e.what());

		ClearGeometry();
		Q_EMIT GeometryChanged();
		return;
	}

	m_SubmeshRefs.push_back({ 0, 0, 0, true });  // AddSphereGeom writes a tangent
	// Not localized: the submesh selector's itemText() is read back as the stem of an auto-saved
	// material's filename (FlushEditedGraphs), so translating it would make the path locale-dependent.
	m_SubmeshNames = QStringList{ "Sphere" };  // procedural sphere: a single submesh

	m_SubmeshMaterialPaths = QStringList{ QString() };
	FocusOn(glm::vec3(0.0f), 1.0f);

	Q_EMIT GeometryChanged();
}

void
MaterialPreviewWindow::Reset()
{
	ShowDefaultSphere();
	RestoreConfiguredEnvironment();
}

void
MaterialPreviewWindow::LoadMesh(const std::filesystem::path& path)
{
	assetlib::BMesh mesh;
	const QString   name = QString::fromStdString(path.filename().string());

	const QString title = editor::Localize(
		m_Host.GetLanguageResolver(),
		"bernini.material.load_mesh_title",
		"Load Mesh");

	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.material.loading_mesh_title",
			{ name },
			"Loading {0}"),
		[&](background::Progress& progress) {
			progress.Report(
				0,
				0,
				editor::Localize(
					m_Host.GetLanguageResolver(),
					"bernini.material.reading_mesh_progress",
					"Reading mesh..."));
			mesh = editor::LoadMeshThroughSeam(m_Host.GetStore(), path);
			if (mesh.meshes.empty())
				throw std::runtime_error("mesh contains no meshes");
		});

	if (!result.Completed())
	{
		qWarning(
			"MaterialPreview: failed to load mesh '%s': %s",
			path.string().c_str(),
			qPrintable(result.error));

		QMessageBox::warning(
			window(),
			title,
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.material.load_mesh_failed",
				{ name, result.error },
				"Could not load '{0}':\n\n{1}"));

		ShowDefaultSphere();
		return;
	}

	try
	{
		struct Focus
		{
			glm::vec3 center;
			float     radius;
		};

		Focus focus{};
		ClearGeometry();
		m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
			bgl::IScene* scene = &context.scene;

			// The preview authors a material, so bind the same neutral material to every source
			// material slot; the graph then rebinds it per submesh.
			const auto materials = std::vector<bgl::MaterialHandle>(
				std::max<size_t>(1, mesh.materials.size()),
				m_DefaultMaterial);

			// A .bmesh spreads its submeshes across several meshes, and a node instances a mesh (the
			// same mesh can be instanced by several nodes). Upload each mesh once, then place an
			// instance for every node that references one, at that node's world transform.
			auto geomForMesh =
				std::unordered_map<uint32_t, uint32_t>();  // mesh index -> m_Geoms index
			auto raycastGeoms = std::vector<uint32_t>();   // m_Geoms index -> raycaster geometry
			auto aabbMin      = glm::vec3(std::numeric_limits<float>::max());
			auto aabbMax      = glm::vec3(std::numeric_limits<float>::lowest());

			for (uint32_t nodeIndex = 0; nodeIndex < mesh.nodes.size(); ++nodeIndex)
			{
				const assetlib::Node& node = mesh.nodes[nodeIndex];
				if (!bmesh::ReferencesMesh(mesh, node))
					continue;

				auto [it, inserted] =
					geomForMesh.try_emplace(node.mesh, static_cast<uint32_t>(m_Geoms.size()));
				if (inserted)
				{
					m_Geoms.push_back(scene->AddStaticMeshGeom(mesh, node.mesh, materials));
					raycastGeoms.push_back(m_Raycaster.AddMesh(mesh, node.mesh));

					// Name each of this mesh's submeshes once, in the order the selector shows them.
					const assetlib::Mesh& entry = mesh.meshes[node.mesh];
					for (uint32_t i = 0; i < entry.submeshCount; ++i)
					{
						const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];

						const std::string_view pooled = mesh.stringPool.at(submesh.nameOffset);
						auto                   name =
							QString::fromUtf8(pooled.data(), static_cast<qsizetype>(pooled.size()));
						// Not localized, like the sphere's "Sphere" above: this too is read back as a
						// filename stem for an unnamed submesh's auto-saved material.
						if (name.isEmpty())
							name = QString("Submesh %1").arg(m_SubmeshNames.size());
						m_SubmeshNames << name;
						m_SubmeshMaterialPaths << ResolveMaterialPath(mesh, submesh, m_DataRoot);
						m_SubmeshRefs.push_back(
							{ it->second,
						      i,
						      entry.firstSubmesh + i,
						      assetlib::hasTangent(submesh) });
					}
				}

				const glm::mat4 world = bmesh::GetInstanceTransform(mesh, nodeIndex);
				m_Instances.push_back(
					{ view->CreateStaticMeshInstance(m_Geoms[it->second], world), it->second });
				m_Raycaster.AddInstance(raycastGeoms[it->second], world);

				bmesh::GrowBoundsForMesh(mesh, node.mesh, world, aabbMin, aabbMax);
			}

			if (m_Geoms.empty())
				throw std::runtime_error("no node references a mesh");

			const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
			const float     radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);
			focus                  = Focus{ center, radius };
		});

		FocusOn(focus.center, focus.radius);

		m_MeshPath = path;

		Q_EMIT GeometryChanged();
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: failed to load mesh '%s': %s", path.string().c_str(), e.what());

		QMessageBox::warning(
			window(),
			title,
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.material.show_mesh_failed",
				{ name, e.what() },
				"Could not show '{0}':\n\n{1}"));

		ShowDefaultSphere();
	}
}

void
MaterialPreviewWindow::SetSubmeshMaterial(uint32_t submeshIndex, bgl::MaterialHandle material)
{
	m_Viewport->Invoke([&](editor::RenderContext&, const bgl::SceneViewRef& view) {
		if (!material.IsValid() || submeshIndex >= m_SubmeshRefs.size())
			return;

		const SubmeshRef& ref = m_SubmeshRefs[submeshIndex];
		if (ref.geomIndex >= m_Geoms.size() || !m_Geoms[ref.geomIndex].IsValid())
			return;

		try
		{
			// An override on the instances, not Scene::SetSubmeshMaterial on the geom. The geom's
			// default is the *asset's* material: rewriting it here would edit the .bmesh's binding as
			// a side effect of typing.
			for (const SubmeshTarget& target :
			     GetInstanceTargets(m_SubmeshRefs, m_Instances, submeshIndex))
			{
				view->SetSubmeshMaterialOverride(target.instance, target.submeshIndex, material);
			}
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialPreview: SetSubmeshMaterial(%u) failed: %s", submeshIndex, e.what());
		}
	});
}

std::vector<MaterialPreviewWindow::SubmeshTarget>
MaterialPreviewWindow::GetInstanceTargets(
	std::span<const SubmeshRef>  refs,
	std::span<const InstanceRef> instances,
	uint32_t                     submeshIndex)
{
	auto targets = std::vector<SubmeshTarget>();

	if (submeshIndex >= refs.size())
		return targets;

	targets.reserve(instances.size());

	const SubmeshRef& ref = refs[submeshIndex];
	for (const InstanceRef& instance : instances)
	{
		if (instance.geomIndex == ref.geomIndex && instance.handle.IsValid())
			targets.push_back({ instance.handle, ref.localSubmesh });
	}

	return targets;
}

void
MaterialPreviewWindow::SetSelectedSubmesh(std::optional<uint32_t> submeshIndex)
{
	m_Viewport->Invoke([&](editor::RenderContext&, const bgl::SceneViewRef& view) {
		try
		{
			view->ClearSelection();

			if (!submeshIndex.has_value())
				return;

			for (const SubmeshTarget& target :
			     GetInstanceTargets(m_SubmeshRefs, m_Instances, *submeshIndex))
			{
				view->SetSubmeshSelected(target.instance, target.submeshIndex, true);
			}
		}
		catch (const std::exception& e)
		{
			qWarning(
				"MaterialPreview: SetSelectedSubmesh(%u) failed: %s",
				submeshIndex.value_or(0xFFFFFFFFu),
				e.what());
		}
	});
}

bool
MaterialPreviewWindow::AcceptsDrop(const QMimeData* mime)
{
	return editor::IsMeshDrag(mime) || !FirstEnvironmentUrl(mime).isEmpty();
}

bool
MaterialPreviewWindow::TakeDrop(const QMimeData* mime)
{
	if (const QString environment = FirstEnvironmentUrl(mime); !environment.isEmpty())
	{
		SetEnvironment(environment.toStdString());
		return true;
	}

	const QString mesh =
		editor::MeshForDrop(m_Host, mime, QString::fromStdWString(m_DataRoot.wstring()));
	if (mesh.isEmpty())
		return false;

	LoadMesh(std::filesystem::path(mesh.toStdWString()));
	return true;
}

void
MaterialPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (AcceptsDrop(event->mimeData()))
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (AcceptsDrop(event->mimeData()))
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::dropEvent(QDropEvent* event)
{
	if (TakeDrop(event->mimeData()))
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::SetEnvironment(const std::string& benvPath)
{
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		editor::BindEnvironment(
			&context.scene,
			view.Get(),
			m_Environment,
			benvPath,
			m_Host.GetStore(),
			"MaterialPreview");
	});
}

void
MaterialPreviewWindow::BindConfiguredEnvironment()
{
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		if (!m_Environment.configured.environmentMap.empty())
		{
			try
			{
				auto external = std::optional<assetlib::AssetStore>();
				if (!m_Environment.configured.dataRoot.empty() &&
				    m_Environment.configured.dataRoot != m_Host.GetStore().GetDataRoot())
					external.emplace(m_Environment.configured.dataRoot);
				const auto& store = external ? *external : m_Host.GetStore();

				editor::BindEnvironment(
					&context.scene,
					view.Get(),
					m_Environment,
					m_Environment.configured.environmentMap,
					store,
					"MaterialPreview");
			}
			catch (const std::exception& error)
			{
				qWarning("Configured environment could not be loaded: %s", error.what());
			}
		}
	});
}

void
MaterialPreviewWindow::RestoreConfiguredEnvironment()
{
	const std::optional<std::string> restore = editor::GetEnvironmentToRestore(m_Environment);
	if (!restore)
		return;

	BindConfiguredEnvironment();
}

void
MaterialPreviewWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	UpdateCamera();
}

void
MaterialPreviewWindow::FocusOn(const glm::vec3& center, float radius)
{
	m_Orbit.FocusOn(center, radius);
	UpdateCamera();
}

void
MaterialPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_DragButton   = event->button();
	m_LastMousePos = event->position().toPoint();
	m_PressPos     = m_LastMousePos;
	m_Dragged      = false;
}

void
MaterialPreviewWindow::mouseReleaseEvent(QMouseEvent* event)
{
	// A press the camera never followed is a click: pick what it landed on.
	if (event->button() == Qt::LeftButton && m_DragButton == Qt::LeftButton && !m_Dragged)
		PickAt(event->position());

	m_DragButton = Qt::NoButton;
}

void
MaterialPreviewWindow::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos   = event->position().toPoint();
	const QPoint delta = pos - m_LastMousePos;
	m_LastMousePos     = pos;

	if (m_DragButton != Qt::NoButton &&
	    (pos - m_PressPos).manhattanLength() > QApplication::startDragDistance())
		m_Dragged = true;

	if (m_DragButton == Qt::LeftButton)
	{
		m_Orbit.Orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	}
	else if (m_DragButton == Qt::MiddleButton || m_DragButton == Qt::RightButton)
	{
		m_Orbit.Pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
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
	m_Orbit.Dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
	UpdateCamera();
	event->accept();
}

void
MaterialPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	m_Camera = m_Orbit.GetCamera(aspect);
	m_Viewport->SetCamera(m_Camera);
}

void
MaterialPreviewWindow::PickAt(const QPointF& pixel)
{
	if (width() <= 0 || height() <= 0)
		return;

	// Logical pixels against the logical size: NDC is scale-invariant, so the device pixel ratio
	// and the render scale never enter into it.
	const game::Ray ray = game::RayThroughPixel(
		m_Camera.GetViewProjection(),
		glm::vec2(pixel.x(), pixel.y()),
		glm::vec2(width(), height()));

	int index = -1;
	if (const auto hit = m_Raycaster.Raycast(ray);
	    hit.has_value() && hit->instance < m_Instances.size())
	{
		index =
			SelectorIndexOf(m_SubmeshRefs, m_Instances[hit->instance].geomIndex, hit->submeshIndex);
	}

	Q_EMIT SubmeshPicked(index);
}

int
MaterialPreviewWindow::SelectorIndexOf(
	std::span<const SubmeshRef> refs,
	uint32_t                    geomIndex,
	uint32_t                    localSubmesh)
{
	for (size_t i = 0; i < refs.size(); ++i)
	{
		if (refs[i].geomIndex == geomIndex && refs[i].localSubmesh == localSubmesh)
			return static_cast<int>(i);
	}
	return -1;
}

QStringList
MaterialPreviewWindow::GetHeldOpenPaths() const
{
	return editor::GetHeldOpenEnvironment(m_Environment);
}

bool
MaterialPreviewWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != m_Viewport)
		return QWidget::eventFilter(watched, event);
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
		mousePressEvent(static_cast<QMouseEvent*>(event));
		return true;
	case QEvent::MouseButtonRelease:
		mouseReleaseEvent(static_cast<QMouseEvent*>(event));
		return true;
	case QEvent::MouseMove:
		mouseMoveEvent(static_cast<QMouseEvent*>(event));
		return true;
	case QEvent::Wheel:
		wheelEvent(static_cast<QWheelEvent*>(event));
		return true;
	case QEvent::DragEnter:
		dragEnterEvent(static_cast<QDragEnterEvent*>(event));
		return true;
	case QEvent::DragMove:
		dragMoveEvent(static_cast<QDragMoveEvent*>(event));
		return true;
	case QEvent::Drop:
		dropEvent(static_cast<QDropEvent*>(event));
		return true;
	case QEvent::Resize:
		UpdateCamera();
		break;
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}
