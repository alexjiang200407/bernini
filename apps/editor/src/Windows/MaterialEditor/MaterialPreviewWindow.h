#pragma once

#include <QStringList>

#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <bgl/GeomHandle.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

// Skybox + IBL environment for the material preview, sourced from config.json (materialEditor.*).
// Any path left empty is simply skipped.
struct MaterialPreviewEnv
{
	std::string skybox;      // cube-map .ktx2 drawn as the background
	std::string irradiance;  // IBL diffuse irradiance cube
	std::string prefilter;   // IBL specular prefilter cube
	std::string brdfLut;     // IBL BRDF LUT (2D)
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

private:
	void
	UpdateCamera();

	// Frames the camera on a bounding sphere so a dropped mesh of any size fills the viewport.
	void
	FocusOn(const glm::vec3& center, float radius);

	// Removes the current instance + geometry (instance first, so the geom's refcount drops to 0).
	void
	ClearGeometry();

	bgl::GeomHandle         m_Geom;
	bgl::MeshInstanceHandle m_Instance;
	bgl::MaterialHandle     m_DefaultMaterial;
	QStringList             m_SubmeshNames;

	glm::vec3 m_FocusCenter = glm::vec3(0.0f);
	float     m_FocusRadius = 1.0f;
};
