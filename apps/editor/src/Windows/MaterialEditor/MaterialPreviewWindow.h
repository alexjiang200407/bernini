#pragma once

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
	std::string skybox;
	std::string irradiance;
	std::string prefilter;
	std::string brdfLut;
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

	// Applies a material to one submesh of the preview geometry (as the node graph compiles).
	void
	SetSubmeshMaterial(uint32_t submeshIndex, bgl::MaterialHandle material);

	const QStringList&
	SubmeshMaterialPaths() const noexcept
	{
		return m_SubmeshMaterialPaths;
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

	struct SubmeshRef
	{
		uint32_t geomIndex     = 0;
		uint32_t localSubmesh  = 0;
		uint32_t sourceSubmesh = 0;  // index into the .bmesh's submeshes array
	};

	std::vector<bgl::GeomHandle>         m_Geoms;
	std::vector<bgl::MeshInstanceHandle> m_Instances;
	std::vector<SubmeshRef>              m_SubmeshRefs;
	bgl::MaterialHandle                  m_DefaultMaterial;
	QStringList                          m_SubmeshNames;
	QStringList                          m_SubmeshMaterialPaths;
	std::filesystem::path                m_MeshPath;  // empty for the default sphere
	std::filesystem::path                m_DataRoot;  // empty until a project is opened

	glm::vec3 m_FocusCenter = glm::vec3(0.0f);
	float     m_FocusRadius = 1.0f;
	float     m_Distance    = 3.0f;
	float     m_Yaw         = 0.0f;
	float     m_Pitch       = 0.0f;

	QPoint          m_LastMousePos;
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
