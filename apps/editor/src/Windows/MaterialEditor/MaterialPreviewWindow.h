#pragma once

#include "Render/environment.h"

#include <QStringList>

#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <bgl/GeomHandle.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QWheelEvent;

struct MaterialPreviewEnv
{
	std::string environmentMap;

	// What the paths inside that `.benv` resolve against. Configured rather than derived from the
	// file: an environment is not always two levels under the root it belongs to.
	std::filesystem::path dataRoot;

	// Absent means the exposure the `.benv` carries, which is the value derived from those maps.
	// Set it only to overrule that deliberately.
	std::optional<float> exposureOverride;

	// A material editor wants the eye on the material, and a defocused backdrop reads as depth of
	// field where a sharp one competes for attention -- so this viewport overrules the `.bsky`'s own
	// presentation by default. A sky baked as a single mip cannot honour it and stays as it is.
	std::optional<uint32_t> skyMipLevelOverride = 3;
};

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

	// Restores the default sphere (shown when no mesh is selected).
	void
	ShowDefaultSphere();

Q_SIGNALS:
	// The preview geometry changed, so its submeshes did too.
	void
	GeometryChanged();

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

	/**
	 * Lights the preview from `benvPath`, releasing whatever the last one bound.
	 *
	 * Without the release each dropped environment would keep its predecessor's three cube maps
	 * uploaded for the life of the window, and the scene's texture slots are bounded.
	 */
	void
	SetEnvironment(const std::string& benvPath);

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

	glm::vec3
	EyePosition() const;

	void
	FocusOn(const glm::vec3& center, float radius);

	void
	ClearGeometry();

	std::vector<bgl::GeomHandle> m_Geoms;
	std::vector<InstanceRef>     m_Instances;
	std::vector<SubmeshRef>      m_SubmeshRefs;
	bgl::MaterialHandle          m_DefaultMaterial;
	QStringList                  m_SubmeshNames;
	QStringList                  m_SubmeshMaterialPaths;
	std::filesystem::path        m_MeshPath;  // empty for the default sphere
	std::filesystem::path        m_DataRoot;  // empty until a project is opened

	// What the last ApplyEnvironment bound, so the next one can release it.
	editor::AppliedEnvironment m_Environment;
	std::optional<float>       m_ExposureOverride;
	std::optional<uint32_t>    m_SkyMipLevelOverride;
	std::filesystem::path      m_ConfiguredRoot;  // stands in until a project is opened

	glm::vec3 m_FocusCenter = glm::vec3(0.0f);
	float     m_FocusRadius = 1.0f;
	float     m_Distance    = 3.0f;
	float     m_Yaw         = 0.0f;
	float     m_Pitch       = 0.0f;

	QPoint          m_LastMousePos;
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
