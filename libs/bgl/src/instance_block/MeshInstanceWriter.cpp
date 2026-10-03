#include "instance_block/MeshInstanceWriter.h"
#include <algorithm>
#include <bgl/GeomType.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <cctype>
#include <core/math.h>
#include <format>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		bool
		IsIdentifier(std::string_view name) noexcept
		{
			if (name.empty() || (std::isdigit(static_cast<unsigned char>(name.front())) != 0))
				return false;

			return std::ranges::all_of(name, [](char c) {
				return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
			});
		}

		std::string_view
		WriterInterface(GeomType geomType) noexcept
		{
			return geomType == GeomType::kSkinnedMesh ? "ISkinnedMeshInstanceWriter" :
			                                            "IMeshInstanceWriter";
		}
	}

	MeshInstanceWriter::MeshInstanceWriter(
		MeshInstanceWriterDesc        desc,
		bgpu::ComputeKernel           kernel,
		const bgpu::IResourceManager* owner) noexcept :
		m_Desc(std::move(desc)), m_Kernel(std::move(kernel)), m_Owner(owner)
	{}

	static_assert(
		MeshInstanceWriter::c_GroupSize * 65535u >= c_MaxMeshInstanceBlockCapacity,
		"a full block must fit one dispatch dimension");

	uint32_t
	MeshInstanceWriter::DispatchGroups(uint32_t capacity) noexcept
	{
		return core::div_ceil(capacity, c_GroupSize);
	}

	bool
	IsMeshInstanceWriterDescValid(const MeshInstanceWriterDesc& desc) noexcept
	{
		if (!IsIdentifier(desc.slangTypeName) || desc.slangModuleName.empty())
			return false;
		if (desc.geomType != GeomType::kStaticMesh && desc.geomType != GeomType::kSkinnedMesh)
			return false;

		auto rest = std::string_view(desc.slangModuleName);
		while (true)
		{
			const size_t dot = rest.find('.');
			if (!IsIdentifier(rest.substr(0, dot)))
				return false;
			if (dot == std::string_view::npos)
				return true;
			rest.remove_prefix(dot + 1);
		}
	}

	std::string
	MeshInstanceWriterProgramName(const MeshInstanceWriterDesc& desc)
	{
		auto module = desc.slangModuleName;
		std::ranges::replace(module, '.', '_');
		return std::format(
			"programs.mesh_instance_writer.{}.{}__{}",
			desc.geomType == GeomType::kSkinnedMesh ? "skinned" : "static",
			module,
			desc.slangTypeName);
	}

	std::string
	MeshInstanceWriterProgramSource(const MeshInstanceWriterDesc& desc)
	{
		return std::format(
			R"(import bgl.MeshInstanceWriter;
import lib.instance_block.MeshInstanceBufferBlock;
import {0};

struct Uniforms
{{
    MeshInstanceBufferBlock block;
    {1}.Params params;
}};

ConstantBuffer<Uniforms> gUniforms;

// Through the interface, not the type: a type with a Write of its own but conforming to the other
// kind's interface, or to none, is refused here rather than run.
void Run<W : {3}>(MeshInstanceBufferBlock block, W.Params params, uint slot)
{{
    W.Write(block, params, slot);
}}

[shader("compute")]
[numthreads({2}, 1, 1)]
void main(uint slot : SV_DispatchThreadID)
{{
    if (slot >= gUniforms.block.Capacity())
        return;
    Run<{1}>(gUniforms.block, gUniforms.params, slot);
}}
)",
			desc.slangModuleName,
			desc.slangTypeName,
			MeshInstanceWriter::c_GroupSize,
			WriterInterface(desc.geomType));
	}
}
