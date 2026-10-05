#pragma once
#include <bgl/ToonShadingRigLimits.h>  // IWYU pragma: export
#include <bgl/glm.h>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace bgl
{
	/**
	 * One key of a toon-shading-rig edit: the edit's shape when the light comes from `light`. Between
	 * keys an edit is a normalized blend of them, weighted by how near each key's light is to the
	 * face's.
	 *
	 * Everything is in head space -- +X across the face, the axis a mirrored edit flips; +Y up; +Z
	 * out of the face -- in the head's units.
	 */
	struct ToonShadingRigKeyDesc
	{
		// Toward the light. Normalized on upload.
		glm::vec3 light = glm::vec3(0.0f, 0.0f, 1.0f);

		// Where the edit sits: its centre, which it projects onto the surface along the line to it.
		glm::vec3 position = glm::vec3(0.0f);

		// What the edit adds to the half-Lambert term before the cel steps: negative shades,
		// positive lights.
		float gain = 0.0f;

		// The edit's angular size, as seen from `position`. Positive.
		float size = 0.1f;

		// How much the edit stretches along its rotated X, in [0, 1).
		float anisotropy = 0.0f;

		// How hard the edit's edge is along its rotated Y, in [0, 1].
		float sharpness = 0.0f;

		// A twist of the shape about the point (bulge, bend) in its rotated, size-scaled plane, by
		// 10 * (bulge * x + bend * y) radians at (x, y): bend curls it along Y, bulge along X.
		// Dimensionless, any finite value; zero is none.
		float bend  = 0.0f;
		float bulge = 0.0f;

		// The edit's turn about the line from `position`, in radians.
		float rotation = 0.0f;

		// How far from `position` the edit reaches before it fades out. Non-negative.
		float radius = 0.1f;

		// How much the normal the edit falls off with is pulled toward a sphere about the head's
		// origin, in [0, 1]: what keeps an edit whole across small creases.
		float normalSmoothing = 0.5f;

		template <typename Self>
		Self&&
		SetLight(this Self&& self, const glm::vec3& value) noexcept
		{
			self.light = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPosition(this Self&& self, const glm::vec3& value) noexcept
		{
			self.position = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetGain(this Self&& self, float value) noexcept
		{
			self.gain = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSize(this Self&& self, float value) noexcept
		{
			self.size = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAnisotropy(this Self&& self, float value) noexcept
		{
			self.anisotropy = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSharpness(this Self&& self, float value) noexcept
		{
			self.sharpness = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBend(this Self&& self, float value) noexcept
		{
			self.bend = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBulge(this Self&& self, float value) noexcept
		{
			self.bulge = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRotation(this Self&& self, float value) noexcept
		{
			self.rotation = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRadius(this Self&& self, float value) noexcept
		{
			self.radius = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetNormalSmoothing(this Self&& self, float value) noexcept
		{
			self.normalSmoothing = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * One shadow or light shape on the face, keyed on the light's direction in head space.
	 *
	 * An edit whose keys' gains are all of one sign keeps that sign between them; the blend never
	 * turns a shadow into a light.
	 */
	struct ToonShadingRigEditDesc
	{
		// At least one, at most cMaxToonShadingRigKeysPerEdit.
		std::vector<ToonShadingRigKeyDesc> keys;

		// How fast a key's weight falls off as the light turns away from it: the spherical
		// Gaussian's exponent. Positive.
		float keySharpness = 10.0f;

		// The edit and a twin evaluated with head-space X flipped -- keyed for one side of the face,
		// drawn on both. Takes two slots. Its keys must fade to zero gain at front light, or the two
		// meet in the middle and stack.
		bool mirrored = false;

		template <typename Self>
		Self&&
		SetKeys(this Self&& self, std::vector<ToonShadingRigKeyDesc> value) noexcept
		{
			self.keys = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetKeySharpness(this Self&& self, float value) noexcept
		{
			self.keySharpness = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMirrored(this Self&& self, bool value) noexcept
		{
			self.mirrored = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * The light a face sees instead of the sun, measured in head space: azimuth about +Y from +Z,
	 * positive toward +X; elevation above the XZ plane. Radians. The default remaps nothing.
	 *
	 * Only face pixels see it, and the rig's keys are authored against it.
	 */
	struct FaceLightDesc
	{
		// Clamps on the elevation, -pi/2 <= minElevation <= maxElevation <= pi/2.
		float minElevation = -std::numbers::pi_v<float> / 2.0f;
		float maxElevation = std::numbers::pi_v<float> / 2.0f;

		// The clamp on the azimuth's magnitude, in [0, pi].
		float maxAzimuth = std::numbers::pi_v<float>;

		// A light rising from `azimuthFadeStart` to `azimuthFadeEnd` of elevation swings toward the
		// front, by `azimuthFadeAmount` of its azimuth at the top: an overhead sun lights the face
		// from above the brow rather than splitting it down the middle.
		float azimuthFadeStart  = std::numbers::pi_v<float> / 4.0f;
		float azimuthFadeEnd    = std::numbers::pi_v<float> / 2.0f;
		float azimuthFadeAmount = 0.0f;

		template <typename Self>
		Self&&
		SetMinElevation(this Self&& self, float value) noexcept
		{
			self.minElevation = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxElevation(this Self&& self, float value) noexcept
		{
			self.maxElevation = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxAzimuth(this Self&& self, float value) noexcept
		{
			self.maxAzimuth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAzimuthFadeStart(this Self&& self, float value) noexcept
		{
			self.azimuthFadeStart = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAzimuthFadeEnd(this Self&& self, float value) noexcept
		{
			self.azimuthFadeEnd = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetAzimuthFadeAmount(this Self&& self, float value) noexcept
		{
			self.azimuthFadeAmount = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * The normal a face shades its base tone with instead of its mesh's: the surface normal pulled
	 * toward the outward normal of an ellipsoid about the head's origin, so a hard cel step draws
	 * one shadow shape across the face rather than one per crease.
	 *
	 * Only face pixels shade with it. An edit falls off against the surface normal under its own
	 * key's `normalSmoothing`, whatever this is.
	 */
	struct FaceNormalDesc
	{
		// How far the normal is pulled, in [0, 1]: zero is the mesh's own, one the ellipsoid's.
		float smoothing = 0.6f;

		// The ellipsoid's radii along head-space X, Y and Z: the head's half-extents. Positive. Only
		// their proportions matter, and equal radii are a sphere.
		glm::vec3 radii = glm::vec3(1.0f);

		template <typename Self>
		Self&&
		SetSmoothing(this Self&& self, float value) noexcept
		{
			self.smoothing = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRadii(this Self&& self, const glm::vec3& value) noexcept
		{
			self.radii = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * A face's toon shading rig -- the Shading Rig of Petikam, Anjyo & Rhee 2021: art-directed
	 * shadow and light edits on a toon character's face, each keyed on the light's direction in head
	 * space and blended between its keys as the light moves, plus the remapped light and the
	 * smoothed normal the face shades with. One per character, shared by every placement and every
	 * block that takes it.
	 *
	 * Only the toon character model reads it, and only on pixels whose surface sets
	 * `ToonCharacterSurface::face`; no other shading model sees it.
	 *
	 * Which rigged placements are evaluated is chosen on the GPU each frame: only those visible and
	 * whose projected head is larger than `fadeEndPixels`, into a per-view pool of
	 * cToonShadingRigPoolCapacity. Past the pool's capacity, or farther, a placement shades cel only;
	 * which placements win the pool when more ask is unspecified and may change between frames.
	 */
	struct ToonShadingRigDesc
	{
		// The bone whose pose carries the head, an index into the placement's rig. Empty: the
		// placement's own transform is the head's frame, as on a static mesh.
		std::optional<uint32_t> headBoneIndex;

		// Head space to the bone's model space or, with no bone, to the placement's. Affine.
		glm::mat4 headToBone = glm::mat4(1.0f);

		FaceLightDesc faceLight;

		FaceNormalDesc faceNormal;

		// At most cMaxToonShadingRigSlots slots between them.
		std::vector<ToonShadingRigEditDesc> edits;

		// World units, times the placement's uniform scale: what the head's projected size is
		// measured from.
		float headRadius = 0.12f;

		// Projected head diameter in pixels: the edits are whole at `fadeStartPixels` and above,
		// and gone at `fadeEndPixels` and below. 0 <= fadeEndPixels < fadeStartPixels.
		float fadeStartPixels = 96.0f;
		float fadeEndPixels   = 48.0f;

		template <typename Self>
		Self&&
		SetHeadBoneIndex(this Self&& self, std::optional<uint32_t> value) noexcept
		{
			self.headBoneIndex = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetHeadToBone(this Self&& self, const glm::mat4& value) noexcept
		{
			self.headToBone = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetFaceLight(this Self&& self, const FaceLightDesc& value) noexcept
		{
			self.faceLight = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetFaceNormal(this Self&& self, const FaceNormalDesc& value) noexcept
		{
			self.faceNormal = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetEdits(this Self&& self, std::vector<ToonShadingRigEditDesc> value) noexcept
		{
			self.edits = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetHeadRadius(this Self&& self, float value) noexcept
		{
			self.headRadius = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetFadeStartPixels(this Self&& self, float value) noexcept
		{
			self.fadeStartPixels = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetFadeEndPixels(this Self&& self, float value) noexcept
		{
			self.fadeEndPixels = value;
			return std::forward<Self>(self);
		}
	};
}
