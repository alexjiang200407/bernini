#pragma once
#include "constants/constants.h"
#include "resource/Buffer.h"
#include "resource/Sampler.h"
#include "resource/Shader.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include "uniforms/DescriptorHandle.h"
#include "uniforms/UniformLayoutEntry.h"
#include <bgl_common/UniformMirror.h>
#include <core/err/util.h>

namespace bgl
{
	class IMeshletPipeline;
	class IComputePipeline;

	/**
	 * One constant buffer's mirror as this renderer binds it: the shared layout walk, plus the root
	 * parameter D3D12 binds the bytes at. The handle types it accepts are the specialisations below.
	 */
	class Uniforms final : public UniformMirror
	{
	public:
		Uniforms() = default;
		Uniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName);
		Uniforms(IComputePipeline const* pipeline, std::string_view cbufferName);

		Uniforms(const Uniforms&) = delete;
		Uniforms(Uniforms&&)      = default;

		Uniforms&
		operator=(Uniforms&&) = default;

		Uniforms&
		operator=(const Uniforms&) = delete;

		[[nodiscard]] uint32_t
		GetRootParamIndex() const
		{
			return m_RootParamIndex;
		}

	private:
		explicit Uniforms(UniformLayoutEntry entry);

	private:
		uint32_t m_RootParamIndex = 0xFFFFFFFF;
	};

	// Stored as the uint2 the shader reads; the alignment DescriptorHandle carries on Metal is what
	// keeps the mapping out of the neutral half.
	template <>
	struct UniformValueMap<DescriptorHandle>
	{
		static constexpr UniformValueType c_Value = UniformValueType::kDescriptorHandle;
	};

	template <>
	struct UniformAssign<BufferHandle>
	{
		static void
		Assign(UniformMirror::Accessor accessor, BufferHandle handle)
		{
			if (accessor.GetType() == UniformType::kStruct &&
			    accessor.GetSize() == detail::ValueTypeSize(UniformValueType::kDescriptorHandle))
			{
				for (const auto key : c_SmartBufferUniformIndices)
				{
					if (accessor[key].IsValid())
					{
						accessor[key].AssignDescriptorIndex(handle.bindlessIndex);
						return;
					}
				}

				core::throw_runtime_error(
					"Accessor at offset {} is not a valid buffer",
					accessor.GetOffset());
			}

			accessor.AssignDescriptorIndex(handle.bindlessIndex);
		}
	};

	template <>
	struct UniformAssign<BufferSrvHandle>
	{
		static void
		Assign(UniformMirror::Accessor accessor, BufferSrvHandle handle)
		{
			// Only the descriptor travels: this handle's slot indexes the view pool, not the buffer
			// pool, and naming it as a buffer's would be a lie waiting to be believed.
			UniformAssign<BufferHandle>::Assign(accessor, BufferHandle{ {}, handle.bindlessIndex });
		}
	};

	// A raw arena's two descriptors from one assignment: the members below are each a struct of one
	// handle, so each lands through the BufferHandle rule.
	template <>
	struct UniformAssign<RawArenaBinding>
	{
		static void
		Assign(UniformMirror::Accessor accessor, const RawArenaBinding& arena)
		{
			accessor["raw"]     = arena.buffer;
			accessor["handles"] = arena.handles;
		}
	};

	template <>
	struct UniformAssign<SamplerHandle>
	{
		static void
		Assign(UniformMirror::Accessor accessor, SamplerHandle handle)
		{
			accessor.AssignDescriptorIndex(handle.bindlessIndex);
		}
	};

	template <>
	struct UniformAssign<SrvHandle>
	{
		static void
		Assign(UniformMirror::Accessor accessor, SrvHandle handle)
		{
			accessor.AssignDescriptorIndex(handle.bindlessIndex);
		}
	};

	template <>
	struct UniformAssign<TextureAssetHandle>
	{
		static void
		Assign(UniformMirror::Accessor accessor, TextureAssetHandle handle)
		{
			accessor.AssignDescriptorIndex(handle.shaderIndex);
		}
	};
}
