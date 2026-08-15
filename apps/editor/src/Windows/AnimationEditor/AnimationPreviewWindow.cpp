#include "AnimationPreviewWindow.h"

#include "Async/BackgroundTask.h"
#include "Mesh/BMeshUtil.h"
#include "Render/Renderer.h"
#include "Windows/AnimationEditor/AnimationBindings.h"
#include "Windows/AnimationEditor/animation_draws.h"
#include "util/mime_files.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMesh.h>
#include <gamelib/AssetManager.h>
#include <gamelib/vat_freshness.h>

AnimationPreviewWindow::AnimationPreviewWindow(
	QWidget*                     parent,
	RenderTargetWindowDesc       rt,
	editor::EnvironmentApplyDesc env) : RenderTargetWindow(parent, std::move(rt))
{
	setAcceptDrops(true);

	// Wheel events only reach a widget that can take focus, and the camera needs them to dolly.
	setFocusPolicy(Qt::StrongFocus);

	GetRenderer()->Invoke([&] {
		m_Environment = editor::ApplyEnvironment(
			GetPreviewScene(),
			GetPreviewView(),
			env.environmentMap,
			env.dataRoot,
			env.exposureOverride,
			env.skyMipLevelOverride,
			"AnimationPreview");
	});

	UpdateCamera();
}

AnimationPreviewWindow::~AnimationPreviewWindow() { ClearGeometry(); }

void
AnimationPreviewWindow::SetAssets(game::AssetManager* assets)
{
	if (assets == m_Assets)
		return;

	// Through the manager that acquired it, before the pointer moves off it.
	ClearGeometry();
	m_Assets = assets;
}

void
AnimationPreviewWindow::Clear()
{
	ClearGeometry();
	SetTime(0.0f);
	Q_EMIT MeshChanged(QString());
	Q_EMIT AnimationSourcesChanged(QStringList(), -1);
	Q_EMIT ClipsChanged({});
}

void
AnimationPreviewWindow::ClearGeometry()
{
	if (m_Assets != nullptr && (!m_Instances.empty() || !m_Geoms.empty() || !m_VatDraws.empty()))
	{
		GetRenderer()->Invoke([&] {
			for (const VatDraw& draw : m_VatDraws)
			{
				try
				{
					m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				}
				catch (const std::exception& e)
				{
					qWarning("AnimationPreview: failed to destroy a VAT instance: %s", e.what());
				}
			}

			for (const bgl::MeshInstanceHandle& instance : m_Instances)
			{
				try
				{
					m_Assets->DestroyInstance(GetPreviewViewRef(), instance);
				}
				catch (const std::exception& e)
				{
					qWarning("AnimationPreview: failed to destroy an instance: %s", e.what());
				}
			}

			for (const bgl::GeomHandle& geom : m_Geoms)
			{
				try
				{
					m_Assets->ReleaseGeom(geom);
				}
				catch (const std::exception& e)
				{
					qWarning("AnimationPreview: failed to release a geom: %s", e.what());
				}
			}
		});
	}

	m_VatDraws.clear();
	m_Instances.clear();
	m_Geoms.clear();
}

void
AnimationPreviewWindow::LoadMesh(
	const std::filesystem::path& absolutePath,
	const std::string&           animationsRelPath)
{
	const QString name = QString::fromStdString(absolutePath.filename().string());

	if (m_Assets == nullptr || m_DataRoot.empty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("Open a project before loading a mesh."));
		return;
	}

	const auto relPath = absolutePath.lexically_relative(m_DataRoot).lexically_normal();
	const auto rel     = relPath.generic_string();
	if (rel.empty() || rel.starts_with(".."))
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("'%1' is outside the project's Data directory.").arg(name));
		return;
	}

	assetlib::BMesh           mesh;
	editor::AnimationBindings bindings;
	std::string               animations = animationsRelPath;

	// The mesh read, the candidate scan and -- the expensive part -- a stale rig's re-bake, all
	// off the UI and render threads. AcquireVatMesh afterwards finds the .bvat fresh and only
	// uploads.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Loading %1").arg(name),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Reading mesh...");
			mesh = assetlib::load(absolutePath);
			if (mesh.meshes.empty())
				throw std::runtime_error("mesh contains no meshes");

			progress.Report(0, 0, "Resolving animations...");
			bindings = editor::ResolveAnimationBindings(m_DataRoot, rel);
			if (animations.empty() && !bindings.animations.empty())
				animations = bindings.animations.front();

			if (!animations.empty())
			{
				progress.Report(0, 0, "Baking animation textures...");
				(void)game::EnsureVatBaked(m_DataRoot, rel, animations);
			}
		});

	if (!result.Completed())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("Could not load '%1':\n\n%2").arg(name, result.error));
		return;
	}

	try
	{
		struct Loaded
		{
			glm::vec3                                    center;
			float                                        radius;
			std::vector<game::AssetManager::VatClipInfo> clips;
			QString                                      vatRefusal;  // empty when VAT stood up
		};

		const auto plan = editor::PlanAnimationDraws(mesh);
		if (plan.vat.empty() && plan.statics.empty())
			throw std::runtime_error("no node references a mesh");

		const Loaded loaded = GetRenderer()->Invoke([&] {
			ClearGeometry();

			auto out     = Loaded();
			auto aabbMin = glm::vec3(std::numeric_limits<float>::max());
			auto aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

			const auto acquireStatic = [&](const bmesh::InstancePlacement& placement) {
				const bgl::GeomHandle geom = m_Assets->AcquireMesh(rel, placement.meshIndex);
				m_Geoms.push_back(geom);
				m_Instances.push_back(
					m_Assets->CreateInstance(GetPreviewViewRef(), geom, placement.world));
				bmesh::GrowBoundsForMesh(
					mesh,
					placement.meshIndex,
					placement.world,
					aabbMin,
					aabbMax);
			};

			for (const bmesh::InstancePlacement& placement : plan.statics) acquireStatic(placement);

			for (const bmesh::InstancePlacement& placement : plan.vat)
			{
				// A rig with no clip file anywhere falls back to bind pose as static geometry.
				if (animations.empty())
				{
					acquireStatic(placement);
					continue;
				}

				const game::AssetManager::VatMesh vat =
					m_Assets->AcquireVatMesh(rel, animations, placement.meshIndex);
				m_Geoms.push_back(vat.geom);
				m_VatDraws.push_back(
					{ vat.geom,
				      placement.world,
				      m_Assets->CreateVatInstance(
						  GetPreviewViewRef(),
						  vat.geom,
						  placement.world,
						  bgl::ISceneView::VatInstanceDesc{ 0, 0.0f, 1.0f }) });
				out.clips    = vat.clips;
				m_ActiveClip = 0;
				bmesh::GrowBoundsForMesh(
					mesh,
					placement.meshIndex,
					placement.world,
					aabbMin,
					aabbMax);
			}

			out.center = (aabbMin + aabbMax) * 0.5f;
			out.radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);
			return out;
		});

		m_Orbit.FocusOn(loaded.center, loaded.radius);
		UpdateCamera();
		SetTime(0.0f);

		auto candidates = QStringList();
		for (const std::string& candidate : bindings.animations)
			candidates << QString::fromStdString(candidate);
		const auto active = static_cast<int>(std::distance(
			bindings.animations.begin(),
			std::find(bindings.animations.begin(), bindings.animations.end(), animations)));

		Q_EMIT MeshChanged(QString::fromStdString(rel));
		Q_EMIT AnimationSourcesChanged(candidates, active < candidates.size() ? active : -1);
		Q_EMIT ClipsChanged(editor::ToClipInfos(loaded.clips));

		if (!loaded.vatRefusal.isEmpty())
		{
			QMessageBox::information(
				window(),
				QStringLiteral("Open Mesh"),
				QStringLiteral("'%1' is shown in bind pose -- its clips cannot play:\n\n%2")
					.arg(name, loaded.vatRefusal));
		}
	}
	catch (const std::exception& e)
	{
		qWarning(
			"AnimationPreview: failed to show mesh '%s': %s",
			absolutePath.string().c_str(),
			e.what());

		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("Could not show '%1':\n\n%2").arg(name, QString::fromUtf8(e.what())));

		Clear();
	}
}

void
AnimationPreviewWindow::SetActiveClip(const uint32_t index)
{
	if (m_Assets == nullptr || m_VatDraws.empty() || index == m_ActiveClip)
		return;

	// There is no mutate-instance API by design: a clip switch is destroy + recreate, and the
	// caller rewinds its transport so the new clip starts from its first frame.
	GetRenderer()->Invoke([&] {
		for (VatDraw& draw : m_VatDraws)
		{
			try
			{
				m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				draw.instance = bgl::MeshInstanceHandle();
				draw.instance = m_Assets->CreateVatInstance(
					GetPreviewViewRef(),
					draw.geom,
					draw.world,
					bgl::ISceneView::VatInstanceDesc{ index, 0.0f, 1.0f });
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to switch a VAT instance's clip: %s", e.what());
			}
		}
	});

	m_ActiveClip = index;
}

void
AnimationPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (!editor::FirstLocalFileWithSuffix(event->mimeData(), u".bmesh").isEmpty())
		event->acceptProposedAction();
}

void
AnimationPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (!editor::FirstLocalFileWithSuffix(event->mimeData(), u".bmesh").isEmpty())
		event->acceptProposedAction();
}

void
AnimationPreviewWindow::dropEvent(QDropEvent* event)
{
	const QString file = editor::FirstLocalFileWithSuffix(event->mimeData(), u".bmesh");
	if (file.isEmpty())
		return;

	LoadMesh(std::filesystem::path(file.toStdWString()));
	event->acceptProposedAction();
}

void
AnimationPreviewWindow::resizeEvent(QResizeEvent* event)
{
	RenderTargetWindow::resizeEvent(event);
	UpdateCamera();
}

void
AnimationPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_DragButton   = event->button();
	m_LastMousePos = event->position().toPoint();
}

void
AnimationPreviewWindow::mouseReleaseEvent(QMouseEvent*)
{
	m_DragButton = Qt::NoButton;
}

void
AnimationPreviewWindow::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos   = event->position().toPoint();
	const QPoint delta = pos - m_LastMousePos;
	m_LastMousePos     = pos;

	if (m_DragButton == Qt::LeftButton)
		m_Orbit.Orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	else if (m_DragButton == Qt::MiddleButton || m_DragButton == Qt::RightButton)
		m_Orbit.Pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	else
		return;

	UpdateCamera();
}

void
AnimationPreviewWindow::wheelEvent(QWheelEvent* event)
{
	m_Orbit.Dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
	UpdateCamera();
	event->accept();
}

void
AnimationPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
	SetCamera(m_Orbit.GetCamera(aspect));
}
