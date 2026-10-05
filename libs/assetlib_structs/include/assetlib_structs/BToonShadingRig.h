#pragma once
#include <core/glm.h>
#include <string>
#include <vector>

namespace assetlib
{
	/**
	 * One key of a toon-shading-rig edit, authored. The renderer's counterpart is
	 * `bgl::ToonShadingRigKeyDesc`, which says what each value means and the range it must lie in;
	 * the defaults are the same ones. Head space, in the head's units, except `rotation`, which
	 * the document holds in degrees.
	 */
	struct ToonShadingRigKey
	{
		glm::vec3 light    = glm::vec3(0.0f, 0.0f, 1.0f);
		glm::vec3 position = glm::vec3(0.0f);

		float gain            = 0.0f;
		float size            = 0.1f;
		float anisotropy      = 0.0f;
		float sharpness       = 0.0f;
		float bend            = 0.0f;
		float bulge           = 0.0f;
		float rotation        = 0.0f;  // degrees
		float radius          = 0.1f;
		float normalSmoothing = 0.5f;

		// Every key this reader did not know, at any depth, written back as it was read.
		std::string extraJson = "{}";

		bool
		operator==(const ToonShadingRigKey&) const = default;
	};

	/** One shadow or light shape on the face; `bgl::ToonShadingRigEditDesc` is its counterpart. */
	struct ToonShadingRigEdit
	{
		std::string name;  // the author's label; nothing reads it

		std::vector<ToonShadingRigKey> keys;

		float keySharpness = 10.0f;
		bool  mirrored     = false;

		std::string extraJson = "{}";

		bool
		operator==(const ToonShadingRigEdit&) const = default;
	};

	/**
	 * The remap of the light a face sees, as `bgl::FaceLightDesc` holds it but in degrees. The
	 * defaults remap nothing.
	 */
	struct ToonFaceLight
	{
		float minElevation      = -90.0f;
		float maxElevation      = 90.0f;
		float maxAzimuth        = 180.0f;
		float azimuthFadeStart  = 45.0f;
		float azimuthFadeEnd    = 90.0f;
		float azimuthFadeAmount = 0.0f;

		bool
		operator==(const ToonFaceLight&) const = default;
	};

	/**
	 * The normal a face shades its base tone with, as `bgl::FaceNormalDesc` holds it: how far the
	 * surface's is pulled toward an ellipsoid about the head's origin, and the ellipsoid's radii.
	 */
	struct ToonFaceNormal
	{
		float     smoothing = 0.6f;
		glm::vec3 radii     = glm::vec3(1.0f);

		bool
		operator==(const ToonFaceNormal&) const = default;
	};

	/**
	 * A `.btoonrig`: a face's toon shading rig, authored -- the edits, their keys, the face light's
	 * remap, the face's normal and the head's binding. The renderer's counterpart is
	 * `bgl::ToonShadingRigDesc`, and the defaults are its own, so a document that omits a key draws
	 * exactly as one that spells out the default.
	 *
	 * The head bone is a *name*, for the reason an avatar's legs are: an index is a fact about one
	 * cook of one `.bskel`. It is resolved against the mesh's rig where the two meet.
	 *
	 * Ranges are not checked on read. The renderer checks them where it adds the rig, which is the
	 * one place they are stated.
	 */
	struct BToonShadingRig
	{
		std::vector<ToonShadingRigEdit> edits;

		ToonFaceLight faceLight;

		ToonFaceNormal faceNormal;

		// The bone whose pose carries the head; empty for the placement's own frame.
		std::string headBone;

		// Head space to the bone's model space, or to the placement's with no bone. Stored in the
		// document as four rows of four.
		glm::mat4 headToBone = glm::mat4(1.0f);

		float headRadius      = 0.12f;
		float fadeStartPixels = 96.0f;
		float fadeEndPixels   = 48.0f;

		std::string extraJson = "{}";

		bool
		operator==(const BToonShadingRig&) const = default;
	};
}
