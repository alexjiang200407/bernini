#pragma once
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/types/GeomHandle.h>
#include <bgpu/uniforms/Uniforms.h>
#include <cstdint>
#include <memory>

namespace bgl
{
	/** A view's instance block: its writer and its copy of the writer's constant buffer. */
	struct MeshInstanceBlock
	{
		MeshInstanceBlock() noexcept                    = default;
		MeshInstanceBlock(MeshInstanceBlock&&) noexcept = default;
		MeshInstanceBlock(const MeshInstanceBlock&)     = delete;

		MeshInstanceBlock&
		operator=(MeshInstanceBlock&&) noexcept = default;

		MeshInstanceBlock&
		operator=(const MeshInstanceBlock&) = delete;

		~MeshInstanceBlock() noexcept = default;

		GeomHandle            geom;
		uint32_t              capacity = 0;
		MeshInstanceWriterRef writer;

		// Null while `writer` is.
		std::unique_ptr<bgpu::Uniforms> uniforms;
	};
}
