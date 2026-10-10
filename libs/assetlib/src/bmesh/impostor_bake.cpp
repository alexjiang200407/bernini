#include "bmesh/impostor_bake.h"

#include "bmesh/submesh_read.h"

#include <algorithm>
#include <array>
#include <assetlib/vertex_layout.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

namespace assetlib
{
	namespace
	{
		// Each frame is rasterised at this many samples a side per texel and box-filtered down, so a
		// silhouette's coverage is graded rather than a hard step.
		constexpr uint32_t c_Supersample = 2;
		constexpr uint32_t c_RasterSide  = c_ImpostorFrameTexels * c_Supersample;

		// Texels past a silhouette that take their nearest covered neighbour's surface, so neither
		// bilinear filtering nor a mip blends the empty background into the edge.
		constexpr uint32_t c_DilateTexels = 8;

		struct FrameBasis
		{
			glm::vec3 right;
			glm::vec3 up;
			glm::vec3 toViewer;
		};

		[[nodiscard]] FrameBasis
		basisOf(const glm::vec3& toViewer) noexcept
		{
			const glm::vec3 right =
				std::abs(toViewer.y) > 0.9999f ?
					glm::vec3(1.0f, 0.0f, 0.0f) :
					glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), toViewer));
			return { right, glm::cross(toViewer, right), toViewer };
		}

		[[nodiscard]] std::array<float, 256>
		srgbToLinearTable() noexcept
		{
			auto table = std::array<float, 256>();
			for (size_t i = 0; i < table.size(); ++i)
			{
				const float c = static_cast<float>(i) / 255.0f;
				table[i]      = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
			}
			return table;
		}

		/** A byte as the unit float it stores, for a data image. */
		[[nodiscard]] std::array<float, 256>
		unormTable() noexcept
		{
			auto table = std::array<float, 256>();
			for (size_t i = 0; i < table.size(); ++i) table[i] = static_cast<float>(i) / 255.0f;
			return table;
		}

		struct Tables
		{
			std::array<float, 256> srgb  = srgbToLinearTable();
			std::array<float, 256> unorm = unormTable();
		};

		/** Bilinear and repeating, rgb through `toLinear`; alpha is not colour and is not converted. */
		[[nodiscard]] glm::vec4
		sampleLinear(
			const ImpostorImage&          image,
			const glm::vec2               uv,
			const std::array<float, 256>& toLinear) noexcept
		{
			const float x  = uv.x * static_cast<float>(image.width) - 0.5f;
			const float y  = uv.y * static_cast<float>(image.height) - 0.5f;
			const float fx = std::floor(x);
			const float fy = std::floor(y);
			const float tx = x - fx;
			const float ty = y - fy;

			const auto texel = [&](const float px, const float py) {
				const auto     w  = static_cast<int64_t>(image.width);
				const auto     h  = static_cast<int64_t>(image.height);
				const auto     ix = ((static_cast<int64_t>(px) % w) + w) % w;
				const auto     iy = ((static_cast<int64_t>(py) % h) + h) % h;
				const uint8_t* p =
					image.rgba.data() +
					(static_cast<size_t>(iy) * image.width + static_cast<size_t>(ix)) * 4;
				return glm::vec4(
					toLinear[p[0]],
					toLinear[p[1]],
					toLinear[p[2]],
					static_cast<float>(p[3]) / 255.0f);
			};

			const glm::vec4 top = glm::mix(texel(fx, fy), texel(fx + 1.0f, fy), tx);
			const glm::vec4 bottom =
				glm::mix(texel(fx, fy + 1.0f), texel(fx + 1.0f, fy + 1.0f), tx);
			return glm::mix(top, bottom, ty);
		}

		/** One raster sample of a frame: what the nearest surface there is. */
		struct Sample
		{
			glm::vec3 albedo    = glm::vec3(0.0f);
			glm::vec3 normal    = glm::vec3(0.0f);
			float     occlusion = 1.0f;
			float     roughness = 1.0f;
			float     metallic  = 0.0f;
			float     depth     = -std::numeric_limits<float>::infinity();
			bool      covered   = false;
		};

		/** One texel of the first mip, before quantising. */
		struct Texel
		{
			glm::vec3 albedo    = glm::vec3(0.0f);
			glm::vec3 normal    = glm::vec3(0.0f, 1.0f, 0.0f);
			float     occlusion = 1.0f;
			float     depth     = 0.0f;
			float     coverage  = 0.0f;
		};

		/** Every covered sample's roughness and metallic, which the record averages. */
		struct SurfaceSums
		{
			double roughness = 0.0;
			double metallic  = 0.0;
			double samples   = 0.0;
		};

		struct Vertex
		{
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec2 uv;
			glm::vec2 uv1;
		};

		struct Triangles
		{
			std::vector<Vertex>   vertices;
			std::vector<uint32_t> indices;
			uint32_t              surface = 0;
		};

		[[nodiscard]] std::vector<Triangles>
		readTriangles(const ImpostorSource& source)
		{
			auto out = std::vector<Triangles>();
			for (size_t s = 0; s < source.submeshes.size(); ++s)
			{
				const Submesh&         submesh = source.submeshes[s];
				const VertexAttribute* position =
					findAttribute(submesh.layout, VertexSemantic::kPosition);
				if (position == nullptr || position->format != VertexFormat::kFloat32x3)
					core::throw_runtime_error("impostor bake: submesh {} has no float position", s);
				const VertexAttribute* normal =
					findAttribute(submesh.layout, VertexSemantic::kNormal);
				const VertexAttribute* uv =
					findAttribute(submesh.layout, VertexSemantic::kTexCoord0);
				const VertexAttribute* uv1 =
					findAttribute(submesh.layout, VertexSemantic::kTexCoord1);
				if (normal != nullptr && normal->format != VertexFormat::kFloat32x3)
					normal = nullptr;
				if (uv != nullptr && uv->format != VertexFormat::kFloat32x2)
					uv = nullptr;
				if (uv1 != nullptr && uv1->format != VertexFormat::kFloat32x2)
					uv1 = nullptr;

				const size_t end = static_cast<size_t>(submesh.vertexByteOffset) +
				                   static_cast<size_t>(submesh.vertexCount) * submesh.layout.stride;
				if (end > source.vertexData.size())
					core::throw_runtime_error(
						"impostor bake: submesh {}'s vertices run past the pool",
						s);

				auto tris    = Triangles();
				tris.surface = static_cast<uint32_t>(s);
				tris.indices = readSubmeshIndices(source.indexData, submesh);
				for (const uint32_t index : tris.indices)
					if (index >= submesh.vertexCount)
						core::throw_runtime_error(
							"impostor bake: an index points outside submesh {}",
							s);

				tris.vertices.reserve(submesh.vertexCount);
				for (uint32_t v = 0; v < submesh.vertexCount; ++v)
				{
					const float* p      = floatsAt(source.vertexData, submesh, *position, v);
					auto         vertex = Vertex{ glm::vec3(p[0], p[1], p[2]),
						                          glm::vec3(0.0f),
						                          glm::vec2(0.0f),
						                          glm::vec2(0.0f) };
					if (normal != nullptr)
					{
						const float* n = floatsAt(source.vertexData, submesh, *normal, v);
						vertex.normal  = glm::vec3(n[0], n[1], n[2]);
					}
					if (uv != nullptr)
					{
						const float* t = floatsAt(source.vertexData, submesh, *uv, v);
						vertex.uv      = glm::vec2(t[0], t[1]);
					}
					if (uv1 != nullptr)
					{
						const float* t = floatsAt(source.vertexData, submesh, *uv1, v);
						vertex.uv1     = glm::vec2(t[0], t[1]);
					}
					tris.vertices.push_back(vertex);
				}
				out.push_back(std::move(tris));
			}
			return out;
		}

		void
		rasterFrame(
			const std::vector<Triangles>& meshes,
			const ImpostorSource&         source,
			const glm::vec3&              center,
			const float                   radius,
			const FrameBasis&             basis,
			const Tables&                 tables,
			std::vector<Sample>&          samples)
		{
			std::ranges::fill(samples, Sample());
			const auto side = static_cast<float>(c_RasterSide);

			for (const Triangles& mesh : meshes)
			{
				const ImpostorSurface& surface = source.surfaces[mesh.surface];
				for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
				{
					const std::array<const Vertex*, 3> v = { &mesh.vertices[mesh.indices[i]],
						                                     &mesh.vertices[mesh.indices[i + 1]],
						                                     &mesh.vertices[mesh.indices[i + 2]] };

					// x right and y down in raster samples; z toward the viewer across the sphere.
					auto s = std::array<glm::vec3, 3>();
					for (size_t k = 0; k < 3; ++k)
					{
						const glm::vec3 q = (v[k]->position - center) / radius;
						s[k]              = glm::vec3(
							(glm::dot(q, basis.right) * 0.5f + 0.5f) * side,
							(0.5f - glm::dot(q, basis.up) * 0.5f) * side,
							glm::dot(q, basis.toViewer));
					}

					// Counter-clockwise as the viewer sees it is a front face; y down flips the sign.
					const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) -
					                   (s[2].x - s[0].x) * (s[1].y - s[0].y);
					if (std::abs(area) < 1e-12f)
						continue;
					const bool front = area < 0.0f;
					if (!front && !surface.doubleSided)
						continue;

					glm::vec3 faceNormal = glm::cross(
						v[1]->position - v[0]->position,
						v[2]->position - v[0]->position);
					faceNormal = glm::dot(faceNormal, faceNormal) > 0.0f ?
					                 glm::normalize(faceNormal) :
					                 basis.toViewer;

					const int x0 = std::max(
						0,
						static_cast<int>(std::floor(std::min({ s[0].x, s[1].x, s[2].x }))));
					const int x1 = std::min(
						static_cast<int>(c_RasterSide) - 1,
						static_cast<int>(std::ceil(std::max({ s[0].x, s[1].x, s[2].x }))));
					const int y0 = std::max(
						0,
						static_cast<int>(std::floor(std::min({ s[0].y, s[1].y, s[2].y }))));
					const int y1 = std::min(
						static_cast<int>(c_RasterSide) - 1,
						static_cast<int>(std::ceil(std::max({ s[0].y, s[1].y, s[2].y }))));

					for (int y = y0; y <= y1; ++y)
					{
						for (int x = x0; x <= x1; ++x)
						{
							const float px = static_cast<float>(x) + 0.5f;
							const float py = static_cast<float>(y) + 0.5f;
							const float w0 =
								((s[1].x - px) * (s[2].y - py) - (s[2].x - px) * (s[1].y - py)) /
								area;
							const float w1 =
								((s[2].x - px) * (s[0].y - py) - (s[0].x - px) * (s[2].y - py)) /
								area;
							const float w2 = 1.0f - w0 - w1;
							if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
								continue;

							Sample& sample = samples
								[static_cast<size_t>(y) * c_RasterSide + static_cast<size_t>(x)];
							const float depth = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
							if (depth <= sample.depth)
								continue;

							const glm::vec2 uv  = w0 * v[0]->uv + w1 * v[1]->uv + w2 * v[2]->uv;
							const glm::vec2 uv1 = w0 * v[0]->uv1 + w1 * v[1]->uv1 + w2 * v[2]->uv1;

							glm::vec4 color = surface.baseColorFactor;
							if (surface.baseColor != nullptr)
								color *= sampleLinear(*surface.baseColor, uv, tables.srgb);
							if (surface.alphaTest && color.a < surface.alphaCutoff)
								continue;

							float occlusion = 1.0f;
							if (surface.occlusion != nullptr)
							{
								const float r = sampleLinear(
													*surface.occlusion,
													surface.occlusionTexCoord == 1 ? uv1 : uv,
													tables.unorm)
								                    .r;
								occlusion     = 1.0f + surface.occlusionStrength * (r - 1.0f);
							}
							float roughness = surface.roughnessFactor;
							float metallic  = surface.metallicFactor;
							if (surface.metallicRoughness != nullptr)
							{
								const glm::vec4 mr =
									sampleLinear(*surface.metallicRoughness, uv, tables.unorm);
								roughness *= mr.g;
								metallic *= mr.b;
							}

							glm::vec3 normal =
								w0 * v[0]->normal + w1 * v[1]->normal + w2 * v[2]->normal;
							normal = glm::dot(normal, normal) > 1e-12f ? glm::normalize(normal) :
							                                             faceNormal;
							if (!front)
								normal = -normal;

							sample = Sample{ glm::vec3(color), normal, occlusion, roughness,
								             metallic,         depth,  true };
						}
					}
				}
			}
		}

		/** The frame's samples into its texels of the first mip, at (originX, originY) of `atlas`. */
		void
		resolveFrame(
			const std::vector<Sample>& samples,
			SurfaceSums&               sums,
			std::vector<Texel>&        atlas,
			const uint32_t             originX,
			const uint32_t             originY)
		{
			auto frame = std::vector<Texel>(c_ImpostorFrameTexels * c_ImpostorFrameTexels);
			for (uint32_t ty = 0; ty < c_ImpostorFrameTexels; ++ty)
			{
				for (uint32_t tx = 0; tx < c_ImpostorFrameTexels; ++tx)
				{
					auto     sum     = Texel();
					uint32_t covered = 0;
					sum.normal       = glm::vec3(0.0f);
					sum.occlusion    = 0.0f;
					for (uint32_t sy = 0; sy < c_Supersample; ++sy)
					{
						for (uint32_t sx = 0; sx < c_Supersample; ++sx)
						{
							const Sample& sample = samples
								[(ty * c_Supersample + sy) * c_RasterSide + tx * c_Supersample +
							     sx];
							if (!sample.covered)
								continue;
							++covered;
							sum.albedo += sample.albedo;
							sum.normal += sample.normal;
							sum.occlusion += sample.occlusion;
							sum.depth += sample.depth;
							sums.roughness += sample.roughness;
							sums.metallic += sample.metallic;
							sums.samples += 1.0;
						}
					}
					Texel& texel = frame[ty * c_ImpostorFrameTexels + tx];
					if (covered == 0)
						continue;
					const float n   = static_cast<float>(covered);
					texel.albedo    = sum.albedo / n;
					texel.normal    = glm::dot(sum.normal, sum.normal) > 1e-12f ?
					                      glm::normalize(sum.normal) :
					                      glm::vec3(0.0f, 1.0f, 0.0f);
					texel.occlusion = sum.occlusion / n;
					texel.depth     = sum.depth / n;
					texel.coverage  = n / static_cast<float>(c_Supersample * c_Supersample);
				}
			}

			// Grown a texel a pass from what is covered, inside the frame only: a neighbouring frame is
			// another view.
			auto filled = std::vector<bool>(frame.size());
			for (size_t i = 0; i < frame.size(); ++i) filled[i] = frame[i].coverage > 0.0f;
			for (uint32_t pass = 0; pass < c_DilateTexels; ++pass)
			{
				auto next = filled;
				for (uint32_t ty = 0; ty < c_ImpostorFrameTexels; ++ty)
				{
					for (uint32_t tx = 0; tx < c_ImpostorFrameTexels; ++tx)
					{
						const size_t i = ty * c_ImpostorFrameTexels + tx;
						if (filled[i])
							continue;
						for (const auto [dx, dy] : { std::pair{ -1, 0 },
						                             std::pair{ 1, 0 },
						                             std::pair{ 0, -1 },
						                             std::pair{ 0, 1 } })
						{
							const int nx = static_cast<int>(tx) + dx;
							const int ny = static_cast<int>(ty) + dy;
							if (nx < 0 || ny < 0 || nx >= static_cast<int>(c_ImpostorFrameTexels) ||
							    ny >= static_cast<int>(c_ImpostorFrameTexels))
								continue;
							const size_t j = static_cast<size_t>(ny) * c_ImpostorFrameTexels +
							                 static_cast<size_t>(nx);
							if (!filled[j])
								continue;
							frame[i]          = frame[j];
							frame[i].coverage = 0.0f;
							next[i]           = true;
							break;
						}
					}
				}
				filled = std::move(next);
			}

			for (uint32_t ty = 0; ty < c_ImpostorFrameTexels; ++ty)
				for (uint32_t tx = 0; tx < c_ImpostorFrameTexels; ++tx)
					atlas[(originY + ty) * c_ImpostorAtlasTexels + originX + tx] =
						frame[ty * c_ImpostorFrameTexels + tx];
		}

		/** The next mip down: coverage averaged, the surface weighted by it where there is any. */
		[[nodiscard]] std::vector<Texel>
		downsample(const std::vector<Texel>& mip, const uint32_t side)
		{
			const uint32_t half = side / 2;
			auto           out  = std::vector<Texel>(static_cast<size_t>(half) * half);
			for (uint32_t y = 0; y < half; ++y)
			{
				for (uint32_t x = 0; x < half; ++x)
				{
					auto  sum       = Texel();
					auto  plain     = Texel();
					float weight    = 0.0f;
					sum.normal      = glm::vec3(0.0f);
					plain.normal    = glm::vec3(0.0f);
					sum.occlusion   = 0.0f;
					plain.occlusion = 0.0f;
					for (uint32_t k = 0; k < 4; ++k)
					{
						const Texel& t = mip[(2 * y + k / 2) * side + 2 * x + k % 2];
						sum.albedo += t.albedo * t.coverage;
						sum.normal += t.normal * t.coverage;
						sum.depth += t.depth * t.coverage;
						sum.occlusion += t.occlusion * t.coverage;
						weight += t.coverage;
						plain.albedo += t.albedo;
						plain.normal += t.normal;
						plain.depth += t.depth;
						plain.occlusion += t.occlusion;
					}
					const Texel& from = weight > 0.0f ? sum : plain;
					const float  n    = weight > 0.0f ? weight : 4.0f;
					Texel&       t    = out[y * half + x];
					t.albedo          = from.albedo / n;
					t.normal          = glm::dot(from.normal, from.normal) > 1e-12f ?
					                        glm::normalize(from.normal) :
					                        glm::vec3(0.0f, 1.0f, 0.0f);
					t.depth           = from.depth / n;
					t.occlusion       = from.occlusion / n;
					t.coverage        = weight / 4.0f;
				}
			}
			return out;
		}

		/** A unit vector on the octahedron, its lower half folded out over the corners, in [-1, 1]. */
		[[nodiscard]] glm::vec2
		octahedral(const glm::vec3& n) noexcept
		{
			const glm::vec3 v = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
			if (v.z >= 0.0f)
				return glm::vec2(v.x, v.y);
			const auto sign = [](const float a) { return a >= 0.0f ? 1.0f : -1.0f; };
			return glm::vec2(
				(1.0f - std::abs(v.y)) * sign(v.x),
				(1.0f - std::abs(v.x)) * sign(v.y));
		}

		[[nodiscard]] uint8_t
		unorm8(const float value) noexcept
		{
			return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		}

		void
		quantise(const std::vector<Texel>& mip, uint8_t* albedo, uint8_t* normalDepth)
		{
			for (size_t i = 0; i < mip.size(); ++i)
			{
				const Texel& t         = mip[i];
				albedo[4 * i]          = unorm8(t.albedo.r);
				albedo[4 * i + 1]      = unorm8(t.albedo.g);
				albedo[4 * i + 2]      = unorm8(t.albedo.b);
				albedo[4 * i + 3]      = unorm8(t.coverage);
				const glm::vec2 e      = octahedral(t.normal);
				normalDepth[4 * i]     = unorm8(e.x * 0.5f + 0.5f);
				normalDepth[4 * i + 1] = unorm8(e.y * 0.5f + 0.5f);
				normalDepth[4 * i + 2] = unorm8(t.occlusion);
				normalDepth[4 * i + 3] = unorm8(t.depth * 0.5f + 0.5f);
			}
		}
	}

	glm::vec3
	impostorFrameDirection(const uint32_t x, const uint32_t y) noexcept
	{
		const auto      last = static_cast<float>(c_ImpostorFramesPerSide - 1);
		const glm::vec2 e =
			glm::vec2(static_cast<float>(x), static_cast<float>(y)) / last * 2.0f - 1.0f;
		const glm::vec2 t = glm::vec2(e.x + e.y, e.x - e.y) * 0.5f;
		return glm::normalize(glm::vec3(t.x, 1.0f - std::abs(t.x) - std::abs(t.y), t.y));
	}

	BakedImpostor
	bakeImpostor(const ImpostorSource& source)
	{
		ZoneScopedN("assetlib impostor bake");
		if (source.surfaces.size() != source.submeshes.size())
			core::throw_runtime_error(
				"impostor bake: {} surfaces for {} submeshes",
				source.surfaces.size(),
				source.submeshes.size());

		const std::vector<Triangles> meshes = readTriangles(source);

		size_t triangles = 0;
		auto   lo        = glm::vec3(std::numeric_limits<float>::infinity());
		auto   hi        = glm::vec3(-std::numeric_limits<float>::infinity());
		for (const Triangles& mesh : meshes)
		{
			triangles += mesh.indices.size() / 3;
			for (const uint32_t index : mesh.indices)
			{
				lo = glm::min(lo, mesh.vertices[index].position);
				hi = glm::max(hi, mesh.vertices[index].position);
			}
		}
		if (triangles == 0)
			core::throw_runtime_error("impostor bake: the mesh has no triangle to bake");

		ZoneTextF(
			"%zu triangles, %u frames",
			triangles,
			c_ImpostorFramesPerSide * c_ImpostorFramesPerSide);

		const glm::vec3 center = (lo + hi) * 0.5f;
		float           radius = 0.0f;
		for (const Triangles& mesh : meshes)
			for (const uint32_t index : mesh.indices)
				radius = std::max(radius, glm::length(mesh.vertices[index].position - center));
		radius = std::max(radius, 1e-6f);

		const auto tables  = Tables();
		auto       sums    = SurfaceSums();
		auto       samples = std::vector<Sample>(static_cast<size_t>(c_RasterSide) * c_RasterSide);
		auto       mip =
			std::vector<Texel>(static_cast<size_t>(c_ImpostorAtlasTexels) * c_ImpostorAtlasTexels);
		for (uint32_t fy = 0; fy < c_ImpostorFramesPerSide; ++fy)
		{
			for (uint32_t fx = 0; fx < c_ImpostorFramesPerSide; ++fx)
			{
				rasterFrame(
					meshes,
					source,
					center,
					radius,
					basisOf(impostorFrameDirection(fx, fy)),
					tables,
					samples);
				resolveFrame(
					samples,
					sums,
					mip,
					fx * c_ImpostorFrameTexels,
					fy * c_ImpostorFrameTexels);
			}
		}

		auto baked   = BakedImpostor();
		baked.record = MeshImpostor{
			.mesh              = 0,
			.albedoOffset      = 0,
			.normalDepthOffset = c_ImpostorAtlasBytes,
			.minPixels         = 0.0f,
			.center            = center,
			.radius            = radius,
			.roughness =
				sums.samples > 0.0 ? static_cast<float>(sums.roughness / sums.samples) : 1.0f,
			.metallic = sums.samples > 0.0 ? static_cast<float>(sums.metallic / sums.samples) : 0.0f
		};
		baked.texels.resize(2 * static_cast<size_t>(c_ImpostorAtlasBytes));

		size_t   offset = 0;
		uint32_t side   = c_ImpostorAtlasTexels;
		for (uint32_t level = 0; level < c_ImpostorAtlasMips; ++level)
		{
			quantise(
				mip,
				baked.texels.data() + offset,
				baked.texels.data() + c_ImpostorAtlasBytes + offset);
			offset += static_cast<size_t>(side) * side * 4;
			if (level + 1 < c_ImpostorAtlasMips)
			{
				mip = downsample(mip, side);
				side /= 2;
			}
		}
		return baked;
	}
}
