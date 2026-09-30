#include "instance_block/InstanceWriter.h"
#include <algorithm>
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
	}

	InstanceWriter::InstanceWriter(
		InstanceWriterDesc            desc,
		bgpu::ComputeKernel           kernel,
		const bgpu::IResourceManager* owner) noexcept :
		m_Desc(std::move(desc)), m_Kernel(std::move(kernel)), m_Owner(owner)
	{}

	static_assert(
		InstanceWriter::c_GroupSize * 65535u >= c_MaxMeshInstanceBlockCapacity,
		"a full block must fit one dispatch dimension");

	uint32_t
	InstanceWriter::DispatchGroups(uint32_t capacity) noexcept
	{
		return core::div_ceil(capacity, c_GroupSize);
	}

	bool
	IsInstanceWriterDescValid(const InstanceWriterDesc& desc) noexcept
	{
		if (!IsIdentifier(desc.type) || desc.module.empty())
			return false;

		auto rest = std::string_view(desc.module);
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
	InstanceWriterProgramName(const InstanceWriterDesc& desc)
	{
		auto module = desc.module;
		std::ranges::replace(module, '.', '_');
		return std::format("programs.instance_writer.{}__{}", module, desc.type);
	}

	std::string
	InstanceWriterProgramSource(const InstanceWriterDesc& desc)
	{
		return std::format(
			R"(import bgl.InstanceWriter;
import lib.instance_block.ViewMeshInstanceBlock;
import {0};

struct Uniforms
{{
    ViewMeshInstanceBlock block;
    {1}.Params params;
}};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads({2}, 1, 1)]
void main(uint slot : SV_DispatchThreadID)
{{
    if (slot >= gUniforms.block.Capacity())
        return;
    {1}.Write(gUniforms.block, gUniforms.params, slot);
}}
)",
			desc.module,
			desc.type,
			InstanceWriter::c_GroupSize);
	}
}
