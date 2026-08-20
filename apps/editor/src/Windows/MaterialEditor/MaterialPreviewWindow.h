#pragma once

#include "Render/OrbitCamera.h"
#include "Render/environment.h"

#include <QStringList>

#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <gamelib/Raycaster.h>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QWheelEvent;

using MaterialPreviewEnv = editor::EnvironmentApplyDesc;

// The right-hand model preview: a lit sphere by default, or a `.bmesh` dropped onto it, shown
// against the configured skybox + IBL with the material being authored applied to it.
class MaterialPreviewWindow : public RenderTargetWindow
{
	Q_OBJECT

public:
	MaterialPreviewWindow(QWidget* parent, RenderTargetWindowDesc rt, MaterialPreviewEnv env);

	// Display names of the current preview geometry's submeshes -- one synthetic entry ("Sphere") for
	// the default sphere, or the submesh names of a dropped mesh. Drives the editor's submesh selector.
	const QStringList&
	SubmeshNames() const noexcept
	{
		return m_SubmeshNames;
	}

	/**
	 * Shows `material` on one submesh of the preview, as the node graph compiles.
	 *
	 * An *instance override*, not a change to the geometry's default: authoring a graph must not
	 * rewrite the shared asset. Committing it to the mesh is a deliberate act -- see the editor's
	 * Set Default Material.
	 */
	void
	SetSubmeshMaterial(uint32_t submeshIndex, bgl::MaterialHandle material);

	struct SubmeshRef
	{
		uint32_t geomIndex     = 0;
		uint32_t localSubmesh  = 0;
		uint32_t sourceSubmesh = 0;  // index into the .bmesh's submeshes array
		bool     hasTangent    = false;
	};

	// A mesh may be instanced by several nodes, so a geom can have more than one instance. An
	// override is per instance, so applying one to a submesh means applying it to every instance of
	// the geom that owns it -- which needs the edge back.
	struct InstanceRef
	{
		bgl::MeshInstanceHandle handle;
		uint32_t                geomIndex = 0;
	};

	struct SubmeshTarget
	{
		bgl::MeshInstanceHandle instance;
		uint32_t                submeshIndex = 0;
	};

	/**
	 * The (instance, submesh) pairs selector index `submeshIndex` acts on: every placed
	 * instance of the geom owning that submesh. Empty when the index is out of range. What a
	 * material override and the selection outline both fan out over. Static so the mapping is
	 * pinnable without a preview window or a device.
	 */
	[[nodiscard]] static std::vector<SubmeshTarget>
	GetInstanceTargets(
		std::span<const SubmeshRef>  refs,
		std::span<const InstanceRef> instances,
		uint32_t                     submeshIndex);

	/**
	 * Makes `submeshIndex` the selection the outline effect contours: every instance of the geom
	 * owning it is marked, anything previously marked is cleared. nullopt -- the editor's
	 * "nothing selected" -- just clears.
	 */
	void
	SetSelectedSubmesh(std::optional<uint32_t> submeshIndex);

	/**
	 * The selector index that owns (`geomIndex`, `localSubmesh`): the inverse of
	 * GetInstanceTargets' fan-out, for a pick that lands on one instance. -1 when nothing
	 * matches. Static so the mapping is pinnable without a preview window or a device.
	 */
	[[nodiscard]] static int
	SelectorIndexOf(std::span<const SubmeshRef> refs, uint32_t geomIndex, uint32_t localSubmesh);

	// The `.bmaterial` each submesh is bound to in the `.bmesh`, absolute. Empty where the mesh names
	// none -- which is what tells a first Save to bind it from a later one that must not.
	const QStringList&
	SubmeshMaterialPaths() const noexcept
	{
		return m_SubmeshMaterialPaths;
	}

	// Records that `submeshIndex` is now bound to `absolutePath` on disk. Called after the `.bmesh`
	// has actually been rewritten, so the two do not drift.
	void
	SetSubmeshMaterialPath(uint32_t submeshIndex, const QString& absolutePath)
	{
		if (submeshIndex < static_cast<uint32_t>(m_SubmeshMaterialPaths.size()))
			m_SubmeshMaterialPaths[static_cast<int>(submeshIndex)] = absolutePath;
	}

	// The project's Data directory. A mesh names its materials relative to it, so the preview cannot
	// resolve them until a project is open.
	void
	SetDataRoot(const std::filesystem::path& dataRoot)
	{
		m_DataRoot = dataRoot;
	}

	const std::filesystem::path&
	MeshPath() const noexcept
	{
		return m_MeshPath;
	}

	uint32_t
	SourceSubmesh(uint32_t submeshIndex) const noexcept;

	/**
	 * Whether the submesh carries a tangent, without which a normal map does nothing: the shader
	 * rebuilds the map's frame from it and falls back to the geometric normal when it is absent.
	 * True for a submesh that does not exist, which has nothing to warn about.
	 */
	[[nodiscard]] bool
	SubmeshHasTangent(uint32_t submeshIndex) const noexcept;

	// Replaces the preview geometry with a baked mesh; falls back to the sphere if it cannot load.
	void
	LoadMesh(const std::filesystem::path& path);

	/**
	 * Back to what the window opens as: the default sphere, lit by the configured environment.
	 *
	 * What leaving the panel does, so the next visit starts from the same place a fresh editor
	 * would rather than from whatever the last one dropped on it.
	 */
	void
	Reset();

Q_SIGNALS:
	// The preview geometry changed, so its submeshes did too.
	void
	GeometryChanged();

	// A click landed on this selector index (-1: empty space). Not applied here: the editor's
	// submesh selector owns the selection, and routing the pick through it keeps the two agreeing.
	void
	SubmeshPicked(int submeshIndex);

protected:
	void
	resizeEvent(QResizeEvent* event) override;

	// A `.bmesh` dragged from the Content Explorer (or the OS) swaps the preview geometry.
	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

	// Lights the preview from a dropped `.benv`, resolved against the open project's data root.
	void
	SetEnvironment(const std::string& benvPath);

	// Puts the environment config.json named back, if a drop displaced it. Part of Reset, because a
	// preview showing the sphere under somebody else's backdrop is showing neither what it was
	// configured with nor what it was asked to show.
	void
	RestoreConfiguredEnvironment();

	void
	mousePressEvent(QMouseEvent* event) override;
	void
	mouseMoveEvent(QMouseEvent* event) override;
	void
	mouseReleaseEvent(QMouseEvent* event) override;
	void
	wheelEvent(QWheelEvent* event) override;

private:
	void
	UpdateCamera();

	// Raycasts the pixel and emits SubmeshPicked with what it found.
	void
	PickAt(const QPointF& pixel);

	void
	FocusOn(const glm::vec3& center, float radius);

	void
	ClearGeometry();

	// Restores the default sphere (shown when no mesh is selected).
	void
	ShowDefaultSphere();

	std::vector<bgl::GeomHandle> m_Geoms;
	std::vector<InstanceRef>     m_Instances;
	std::vector<SubmeshRef>      m_SubmeshRefs;
	bgl::MaterialHandle          m_DefaultMaterial;
	QStringList                  m_SubmeshNames;
	QStringList                  m_SubmeshMaterialPaths;
	std::filesystem::path        m_MeshPath;  // empty for the default sphere
	std::filesystem::path        m_DataRoot;  // empty until a project is opened

	// The configured environment is kept whole because a drop carries only a path and Reset has to
	// be able to get back to it. Its root stands in until a project opens and m_DataRoot names its
	// own.
	editor::EnvironmentBinding m_Environment;

	editor::OrbitCamera m_Orbit;

	// The preview geometry's CPU shadow, rebuilt with it. Its instance i is m_Instances[i]: every
	// CreateStaticMeshInstance is paired with an AddInstance, and ClearGeometry resets both.
	game::Raycaster m_Raycaster;

	bgl::Camera m_Camera;  // the GUI thread's copy; m_RenderCamera lives on the render thread

	QPoint          m_LastMousePos;
	QPoint          m_PressPos;
	bool            m_Dragged    = false;  // the press moved too far to still be a click
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
