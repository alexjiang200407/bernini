#include "scene/Scene.h"
#include "fg/FrameGraph.h"
#include "idl/Constants.h"
#include "idl/Meshlet.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <bgl/PsoType.h>
#include <core/math.h>
#include <numbers>

namespace bgl
{
	namespace
	{
		struct VertexGen
		{
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec2 uv;
			glm::vec4 tangent;
		};

		struct BufferInfo
		{
			std::string_view name;
		};

		// Order MUST stay in lockstep with Scene::GetBuffers() and with
		// ForwardPass's c_ForwardDataBuffers.
		static constexpr std::array<BufferInfo, 6> c_BufferInfo = {
			{ { "scene.submeshBuffer" },
			  { "scene.meshletBuffer" },
			  { "scene.vertexMapBuffer" },
			  { "scene.vertexDataBuffer" },
			  { "scene.indexBuffer" },
			  { "scene.pbrMaterialBuffer" } }
		};

		// The interleaved vertex layout the procedural geometry emits: position,
		// normal, uv, tangent, tightly packed at a 48-byte stride. This is exactly
		// the full VertexGen, and is decoded on the GPU via each submesh's
		// VertexLayout descriptor.
		constexpr uint32_t c_ProceduralStride   = 48;
		constexpr uint32_t c_ProceduralVtxWords = c_ProceduralStride / 4;

		idl::VertexLayout
		MakeProceduralLayout()
		{
			auto layout           = idl::VertexLayout();
			layout.attributeCount = 4;
			layout.stride         = c_ProceduralStride;
			layout.attributes[0]  = { idl::VertexSemantic::kPosition,
				                      idl::VertexFormat::kFloat32x3,
				                      0 };
			layout.attributes[1]  = { idl::VertexSemantic::kNormal,
				                      idl::VertexFormat::kFloat32x3,
				                      12 };
			layout.attributes[2]  = { idl::VertexSemantic::kTexCoord0,
				                      idl::VertexFormat::kFloat32x2,
				                      24 };
			layout.attributes[3]  = { idl::VertexSemantic::kTangent,
				                      idl::VertexFormat::kFloat32x4,
				                      32 };
			return layout;
		}

		// Interleave each vertex's position/normal/uv into the raw byte layout
		// above, returned as uint words for the StructuredBuffer<uint> data buffer.
		std::vector<uint32_t>
		PackVertices(std::span<const VertexGen> verts)
		{
			auto words = std::vector<uint32_t>(verts.size() * c_ProceduralVtxWords);
			for (size_t i = 0; i < verts.size(); ++i)
			{
				std::memcpy(&words[i * c_ProceduralVtxWords], &verts[i], c_ProceduralStride);
			}
			return words;
		}

		std::atomic<uint32_t> g_NextSceneId{ 0 };

		// The PSO bucket cached on a submesh, derived from its geom + material type. An absent
		// material falls back to kNull (unlit) -- the same default the old per-instance path used.
		uint32_t
		SubmeshPso(GeomType geomType, MaterialHandle material)
		{
			const MaterialType type =
				material.IsValid() ? material.materialType : MaterialType::kNull;
			return static_cast<uint32_t>(GetPsoFromGeomAndMaterial(geomType, type));
		}
	}

	Scene::Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager) :
		m_Desc(std::move(desc)), m_ResourceManager(std::move(resourceManager))
	{
		m_NamePrefix = std::format("s{}:", g_NextSceneId.fetch_add(1));

		const auto atLeastOne = [](uint32_t n) -> uint32_t { return n != 0 ? n : 1; };

		const uint32_t maxSubmeshes =
			m_Desc.maxSubmeshes != 0 ? m_Desc.maxSubmeshes : m_Desc.maxMeshlets;

		// The vertex data buffer is a StructuredBuffer<uint>, so the byte budget is
		// rounded up to whole 4-byte words.
		const uint32_t maxVertexWords = (m_Desc.maxVertexBufferByteSize + 3u) / 4u;

		// Geometry assets are CPU-only (they hold the shared submeshes descriptor +
		// refcount); the heavy data lives in the GPU range buffers below.
		m_GeomAssets.reset(m_Desc.maxGeom);

		{
			auto submeshBufferDesc      = RangeBufferDesc();
			submeshBufferDesc.maxCount  = atLeastOne(maxSubmeshes);
			submeshBufferDesc.debugName = "Submesh Buffer";

			m_SubmeshBuffer.Init(std::move(submeshBufferDesc), m_ResourceManager);
		}

		{
			auto meshletBufferDesc      = RangeBufferDesc();
			meshletBufferDesc.maxCount  = atLeastOne(m_Desc.maxMeshlets);
			meshletBufferDesc.debugName = "Meshlet Buffer";

			m_MeshletBuffer.Init(std::move(meshletBufferDesc), m_ResourceManager);
		}

		{
			auto vertexMapBufferDesc      = RangeBufferDesc();
			vertexMapBufferDesc.maxCount  = atLeastOne(m_Desc.maxIndices);
			vertexMapBufferDesc.debugName = "Vertex Map Buffer";

			m_VertexMapBuffer.Init(std::move(vertexMapBufferDesc), m_ResourceManager);
		}

		{
			auto vertexDataBufferDesc      = RangeBufferDesc();
			vertexDataBufferDesc.maxCount  = atLeastOne(maxVertexWords);
			vertexDataBufferDesc.debugName = "Vertex Data Buffer";

			m_VertexDataBuffer.Init(std::move(vertexDataBufferDesc), m_ResourceManager);
		}

		{
			auto indexBufferDesc      = RangeBufferDesc();
			indexBufferDesc.maxCount  = atLeastOne(m_Desc.maxIndices);
			indexBufferDesc.debugName = "Index Buffer";

			m_IndexBuffer.Init(std::move(indexBufferDesc), m_ResourceManager);
		}

		{
			auto pbrBufferDesc      = EntryBufferDesc();
			pbrBufferDesc.maxCount  = atLeastOne(m_Desc.maxPbrMaterials);
			pbrBufferDesc.debugName = "Pbr Material Buffer";

			m_Pbr.Init(std::move(pbrBufferDesc), m_ResourceManager);
		}

		m_Samplers[static_cast<size_t>(StandardSampler::kAnisoLinearWrap)] =
			m_ResourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetMaxAnisotropy(16.f).SetAllAddressModes(
					SamplerAddressMode::Wrap));

		m_Samplers[static_cast<size_t>(StandardSampler::kLinearClamp)] =
			m_ResourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::Clamp));

		// Default material textures: white (base color / ORM -> ao=1, factors drive
		// roughness+metal) and a flat tangent-space normal (0.5,0.5,1).
		m_DefaultTextures[static_cast<size_t>(DefaultTexture::kWhite)] =
			m_ResourceManager->CreateSolidTexture(255, 255, 255, 255);
		m_DefaultTextures[static_cast<size_t>(DefaultTexture::kFlatNormal)] =
			m_ResourceManager->CreateSolidTexture(128, 128, 255, 255);
	}

	void
	Scene::Update(ICommandList* cmdList)
	{
		auto buffers = GetBuffers();
		std::apply(
			[cmdList](auto&... buffer) {
				(..., (buffer.IsInitialized() ? buffer.Update(cmdList) : void()));
			},
			buffers);

		// Flush any textures loaded since the last frame (materials, environment maps).
		m_ResourceManager->FlushPendingTextureUploads(cmdList);
	}

	void
	Scene::AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx)
	{
		std::vector<std::string> updateBuffers;
		ImportResources(fg, updateBuffers);

		PassDesc desc;
		desc.SetName(std::format("Scene Update {}", drawIdx));

		for (const std::string& buffer : updateBuffers)
		{
			desc.AddBufferArg(
				BufferArg{ buffer, BarrierSyncFlag::kCopy, BarrierAccessFlag::kCopyDest });
		}

		desc.SetExec([this](const PassContext& ctx) { Update(ctx.GetCommandList()); });

		fg.AddPass(std::move(desc));
	}

	void
	Scene::ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames)
	{
		resourceNames.reserve(resourceNames.size() + c_BufferInfo.size());

		auto   buffers = GetBuffers();
		size_t i       = 0;
		std::apply(
			[&](auto&... buffer) {
				(..., [&] {
					// Import every buffer (including the GPU-only compute buffer): the
					// Update pass declares them as copy-dest so the graph transitions
					// them, and the FrameGraph tracks the state each is left in.
					std::string name(c_BufferInfo[i++].name);
					fg.ImportBuffer(name, buffer.GetBufferHandle());
					resourceNames.push_back(std::move(name));
				}());
			},
			buffers);
	}

	GeomHandle
	Scene::AddCubeGeom(MaterialHandle material)
	{
		try
		{
			// 6 faces x 4 verts (24 total) so each face carries its own normal, uv
			// and tangent -- an 8-vertex cube can't express per-face attributes.
			struct FaceBasis
			{
				glm::vec3 normal;
				glm::vec3 tangent;  // +u direction; bitangent = cross(normal, tangent)
			};
			static const FaceBasis faces[6] = {
				{ { 1, 0, 0 }, { 0, 0, -1 } },   // +X
				{ { -1, 0, 0 }, { 0, 0, 1 } },   // -X
				{ { 0, 1, 0 }, { 1, 0, 0 } },    // +Y
				{ { 0, -1, 0 }, { 1, 0, 0 } },   // -Y
				{ { 0, 0, 1 }, { 1, 0, 0 } },    // +Z
				{ { 0, 0, -1 }, { -1, 0, 0 } },  // -Z
			};
			// Per-face corners in (s, t) order: BL, BR, TR, TL -- CCW from outside.
			static const glm::vec2 corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

			std::vector<VertexGen> cubeVertices;
			std::vector<uint32_t>  cubeIndices;
			cubeVertices.reserve(24);
			cubeIndices.reserve(36);

			for (const auto& face : faces)
			{
				const glm::vec3 up   = glm::cross(face.normal, face.tangent);
				const uint32_t  base = static_cast<uint32_t>(cubeVertices.size());

				for (const auto& c : corners)
				{
					auto v    = VertexGen();
					v.pos     = face.normal + c.x * face.tangent + c.y * up;
					v.normal  = face.normal;
					v.uv      = glm::vec2((c.x + 1.0f) * 0.5f, (c.y + 1.0f) * 0.5f);
					v.tangent = glm::vec4(face.tangent, 1.0f);
					cubeVertices.push_back(v);
				}

				cubeIndices.push_back(base + 0u);
				cubeIndices.push_back(base + 1u);
				cubeIndices.push_back(base + 2u);
				cubeIndices.push_back(base + 0u);
				cubeIndices.push_back(base + 2u);
				cubeIndices.push_back(base + 3u);
			}

			const auto vertexWords      = PackVertices(cubeVertices);
			const auto baseVertexGlobal = m_VertexDataBuffer.Add(vertexWords);

			auto mapIndices = std::vector<uint32_t>(cubeVertices.size());
			for (uint32_t i = 0; i < mapIndices.size(); ++i)
			{
				mapIndices[i] = i;
			}

			const auto baseMapGlobal   = m_VertexMapBuffer.Add(mapIndices);
			const auto baseIndexGlobal = m_IndexBuffer.Add(cubeIndices);

			auto m = idl::Meshlet();

			m.relativeVertexOffset = 0;
			m.vertexCount          = static_cast<uint8_t>(cubeVertices.size());

			m.relativeIndexOffset = 0;
			m.triangleCount       = static_cast<uint8_t>(cubeIndices.size() / 3);

			m.boundingCenter = glm::vec3{ 0.0f };
			m.boundingRadius = glm::sqrt(3.0f);

			const auto meshletSpan       = std::span<const idl::Meshlet>(&m, 1);
			const auto baseMeshletGlobal = m_MeshletBuffer.Add(meshletSpan);

			auto submesh        = idl::Submesh();
			submesh.layout      = MakeProceduralLayout();
			submesh.meshlets    = baseMeshletGlobal;
			submesh.vertexMap   = baseMapGlobal;
			submesh.vertexData  = baseVertexGlobal;
			submesh.indices     = baseIndexGlobal;
			submesh.vertexCount = static_cast<uint32_t>(cubeVertices.size());
			if (material.IsValid())
			{
				submesh.material = material.handle;
			}
			submesh.pso = SubmeshPso(GeomType::kStaticMesh, material);

			const auto submeshSpan       = std::span<const idl::Submesh>(&submesh, 1);
			const auto baseSubmeshGlobal = m_SubmeshBuffer.Add(submeshSpan);

			auto asset      = GeomAsset();
			asset.submeshes = baseSubmeshGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = m_GeomAssets.allocate_and_emplace(asset);
			retVal.geomType = GeomType::kStaticMesh;

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	GeomHandle
	Scene::AddSphereGeom(
		uint32_t       xSegments,
		uint32_t       ySegments,
		float          radius,
		MaterialHandle material)
	{
		try
		{
			std::vector<VertexGen> sphereVerts;
			std::vector<uint32_t>  sphereIndices;

			for (uint32_t y = 0u; y <= ySegments; ++y)
			{
				for (uint32_t x = 0u; x <= xSegments; ++x)
				{
					constexpr auto pi       = std::numbers::pi_v<float>;
					float          xSegment = static_cast<float>(x) / static_cast<float>(xSegments);
					float          ySegment = static_cast<float>(y) / static_cast<float>(ySegments);
					float          xPos = std::cos(xSegment * 2.0f * pi) * std::sin(ySegment * pi);
					float          yPos = std::cos(ySegment * pi);
					float          zPos = std::sin(xSegment * 2.0f * pi) * std::sin(ySegment * pi);

					// Tangent follows +u (increasing longitude): d(pos)/d(xSegment),
					// normalized. bitangent = cross(normal, tangent), so w = +1.
					const float a = xSegment * 2.0f * pi;

					auto v   = VertexGen();
					v.pos    = glm::vec3(xPos, yPos, zPos) * radius;
					v.normal = glm::normalize(v.pos);
					v.uv     = glm::vec2(xSegment, ySegment);
					v.tangent =
						glm::vec4(glm::normalize(glm::vec3(-std::sin(a), 0.0f, std::cos(a))), 1.0f);
					sphereVerts.push_back(v);
				}
			}

			for (uint32_t y = 0u; y < ySegments; ++y)
			{
				for (uint32_t x = 0u; x < xSegments; ++x)
				{
					sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x);
					sphereIndices.push_back(y * (xSegments + 1u) + x);
					sphereIndices.push_back(y * (xSegments + 1u) + x + 1u);

					sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x);
					sphereIndices.push_back(y * (xSegments + 1u) + x + 1u);
					sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x + 1u);
				}
			}

			const auto vertexWords      = PackVertices(sphereVerts);
			const auto baseVertexGlobal = m_VertexDataBuffer.Add(vertexWords);

			auto                  meshlets = std::vector<idl::Meshlet>();
			std::vector<uint32_t> vertexMap;
			std::vector<uint32_t> localIndices;

			const uint32_t totalTriangles = static_cast<uint32_t>(sphereIndices.size() / 3u);
			uint32_t       trianglesDone  = 0u;

			while (trianglesDone < totalTriangles)
			{
				auto meshlet                 = idl::Meshlet();
				meshlet.relativeVertexOffset = static_cast<uint32_t>(vertexMap.size());
				meshlet.relativeIndexOffset  = static_cast<uint32_t>(localIndices.size());

				std::unordered_map<uint32_t, uint32_t> localRemap;
				uint32_t                               localVertexCount   = 0u;
				uint32_t                               localTriangleCount = 0u;

				while (trianglesDone < totalTriangles)
				{
					const uint32_t triBase = trianglesDone * 3u;
					const uint32_t tri[3]  = { sphereIndices[triBase],
						                       sphereIndices[triBase + 1u],
						                       sphereIndices[triBase + 2u] };

					uint32_t newVertices = 0u;
					for (uint32_t i = 0u; i < 3u; ++i)
					{
						if (!localRemap.contains(tri[i]))
						{
							++newVertices;
						}
					}

					if (localVertexCount + newVertices > idl::cMaxVerticesPerMeshlet ||
					    localTriangleCount + 1u > idl::cMaxPrimsPerMeshlet)
					{
						break;
					}

					for (uint32_t i = 0u; i < 3u; ++i)
					{
						const uint32_t geomVertexIdx = tri[i];
						if (!localRemap.contains(geomVertexIdx))
						{
							localRemap[geomVertexIdx] = localVertexCount++;
							vertexMap.push_back(geomVertexIdx);
						}
						localIndices.push_back(localRemap[geomVertexIdx]);
					}

					++localTriangleCount;
					++trianglesDone;
				}

				meshlet.vertexCount   = static_cast<uint16_t>(localVertexCount);
				meshlet.triangleCount = static_cast<uint16_t>(localTriangleCount);

				auto minBound = glm::vec3(std::numeric_limits<float>::max());
				auto maxBound = glm::vec3(std::numeric_limits<float>::lowest());
				for (const auto& [geomVertexIdx, localIdx] : localRemap)
				{
					minBound = glm::min(minBound, sphereVerts[geomVertexIdx].pos);
					maxBound = glm::max(maxBound, sphereVerts[geomVertexIdx].pos);
				}
				meshlet.boundingCenter = (minBound + maxBound) * 0.5f;
				meshlet.boundingRadius = glm::distance(maxBound, meshlet.boundingCenter);

				meshlets.push_back(meshlet);
			}

			const auto baseMapGlobal     = m_VertexMapBuffer.Add(vertexMap);
			const auto baseIndexGlobal   = m_IndexBuffer.Add(localIndices);
			const auto baseMeshletGlobal = m_MeshletBuffer.Add(meshlets);

			auto submesh        = idl::Submesh();
			submesh.layout      = MakeProceduralLayout();
			submesh.meshlets    = baseMeshletGlobal;
			submesh.vertexMap   = baseMapGlobal;
			submesh.vertexData  = baseVertexGlobal;
			submesh.indices     = baseIndexGlobal;
			submesh.vertexCount = static_cast<uint32_t>(sphereVerts.size());
			if (material.IsValid())
			{
				submesh.material = material.handle;
			}
			submesh.pso = SubmeshPso(GeomType::kStaticMesh, material);

			const auto submeshSpan       = std::span<const idl::Submesh>(&submesh, 1);
			const auto baseSubmeshGlobal = m_SubmeshBuffer.Add(submeshSpan);

			auto asset      = GeomAsset();
			asset.submeshes = baseSubmeshGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = m_GeomAssets.allocate_and_emplace(asset);
			retVal.geomType = GeomType::kStaticMesh;

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	MaterialHandle
	Scene::CreatePbrMaterial(const PbrMaterialDesc& desc)
	{
		const auto white = m_DefaultTextures[static_cast<size_t>(DefaultTexture::kWhite)].slot;
		const auto flatNormal =
			m_DefaultTextures[static_cast<size_t>(DefaultTexture::kFlatNormal)].slot;

		idl::PbrMaterial material{};
		material.baseColorTexture = idl::TextureHandle{ white.index };
		material.normalTexture    = idl::TextureHandle{ flatNormal.index };
		material.ormTexture       = idl::TextureHandle{ white.index };
		material.baseColorFactor  = desc.baseColorFactor;
		material.metallicFactor   = desc.metallicFactor;
		material.roughnessFactor  = desc.roughnessFactor;

		const core::slot_handle slot = m_Pbr.Add(material);
		return MaterialHandle{ MaterialType::kPBR, slot };
	}

	void
	Scene::SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial must be of type kStaticMesh");
		}
		if (!IsGeomSlotValid(geom.handle))
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial has expired or is invalid");
		}
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to SetSubmeshMaterial");
		}

		const GeomAsset& asset = m_GeomAssets[geom.handle.index];
		if (submeshIndex >= asset.submeshes.count)
		{
			throw SceneError("submeshIndex passed to SetSubmeshMaterial is out of range");
		}

		const uint32_t globalIndex = asset.submeshes.range.offsetStart + submeshIndex;

		auto submesh     = m_SubmeshBuffer.AtIndex(globalIndex);
		submesh.material = material.handle;
		submesh.pso      = SubmeshPso(geom.geomType, material);
		m_SubmeshBuffer.SetAtIndex(globalIndex, submesh);
	}

	void
	Scene::DeleteGeom(GeomHandle geom)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError("GeomHandle passed to DeleteGeom must be of type kStaticMesh");
		}

		if (!m_GeomAssets.valid(geom.handle.index, geom.handle.generation))
		{
			throw SceneError("GeomHandle passed to DeleteGeom refers to a deleted or unknown geom");
		}

		const GeomAsset& asset = m_GeomAssets[geom.handle.index];
		if (asset.refCount != 0)
		{
			throw SceneError(
				std::format(
					"Cannot delete geom still referenced by {} live mesh instance(s)",
					asset.refCount));
		}

		// The geometry's per-part ranges live on each Submesh, so free them per
		// submesh before releasing the submesh range itself.
		const uint32_t submeshRoot  = asset.submeshes.range.offsetStart;
		const uint32_t submeshCount = asset.submeshes.count;
		for (uint32_t i = 0; i < submeshCount; ++i)
		{
			const auto& submesh = m_SubmeshBuffer.AtIndex(submeshRoot + i);

			m_VertexDataBuffer.EraseByIndex(submesh.vertexData.offsetStart);
			m_VertexMapBuffer.EraseByIndex(submesh.vertexMap.offsetStart);
			m_IndexBuffer.EraseByIndex(submesh.indices.offsetStart);
			m_MeshletBuffer.EraseByIndex(submesh.meshlets.range.offsetStart);
		}

		m_SubmeshBuffer.EraseByIndex(submeshRoot);
		m_GeomAssets.release_slot(geom.handle.index);
	}

	TextureAssetHandle
	Scene::AddTextureAsset(assetlib::ImageData img, std::string debugName)
	{
		const auto gpuTexture = m_ResourceManager->CreateTexture(img, std::move(debugName));
		return static_cast<TextureAssetHandle>(gpuTexture);
	}
}
