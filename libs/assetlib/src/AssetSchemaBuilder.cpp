#include "AssetSchemaBuilder.h"

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceStamp.h>
#include <assetlib_structs/VertexLayout.h>

namespace assetlib
{
	AssetSchemaBuilder&
	AssetSchemaBuilder::AddTransform()
	{
		return AddLayout<Transform>("Transform", [](auto& layout) {
			layout.AddField("translation", &Transform::translation)
				.AddField("rotation", &Transform::rotation)
				.AddField("scale", &Transform::scale);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddNode()
	{
		return AddLayout<Node>("Node", [](auto& layout) {
			layout.AddField("localTransform", &Node::localTransform)
				.AddField("parent", &Node::parent, c_InvalidIndex)
				.AddField("firstChild", &Node::firstChild, c_InvalidIndex)
				.AddField("nextSibling", &Node::nextSibling, c_InvalidIndex)
				.AddField("mesh", &Node::mesh, c_InvalidIndex)
				.AddField("nameOffset", &Node::nameOffset);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddMesh()
	{
		return AddLayout<Mesh>("Mesh", [](auto& layout) {
			layout.AddField("firstSubmesh", &Mesh::firstSubmesh)
				.AddField("submeshCount", &Mesh::submeshCount)
				.AddField("nameOffset", &Mesh::nameOffset);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddVertexLayout()
	{
		AddLayout<VertexAttribute>("VertexAttribute", [](auto& layout) {
			layout.AddField("semantic", &VertexAttribute::semantic)
				.AddField("format", &VertexAttribute::format)
				.AddField("offset", &VertexAttribute::offset);
		});
		return AddLayout<VertexLayout>("VertexLayout", [](auto& layout) {
			layout.AddField("attributes", &VertexLayout::attributes)
				.AddField("attributeCount", &VertexLayout::attributeCount)
				.AddField("stride", &VertexLayout::stride);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddSubmesh()
	{
		return AddLayout<Submesh>("Submesh", [](auto& layout) {
			layout.AddField("layout", &Submesh::layout)
				.AddField("vertexByteOffset", &Submesh::vertexByteOffset)
				.AddField("vertexCount", &Submesh::vertexCount)
				.AddField("indexByteOffset", &Submesh::indexByteOffset)
				.AddField("indexCount", &Submesh::indexCount)
				.AddField("indexType", &Submesh::indexType)
				.AddField("firstMeshlet", &Submesh::firstMeshlet)
				.AddField("meshletCount", &Submesh::meshletCount)
				.AddField("material", &Submesh::material, c_InvalidIndex)
				.AddField("aabbMin", &Submesh::aabbMin)
				.AddField("aabbMax", &Submesh::aabbMax)
				.AddField("nameOffset", &Submesh::nameOffset);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddMeshlet()
	{
		return AddLayout<Meshlet>("Meshlet", [](auto& layout) {
			layout.AddField("vertexOffset", &Meshlet::vertexOffset)
				.AddField("triangleOffset", &Meshlet::triangleOffset)
				.AddField("vertexCount", &Meshlet::vertexCount)
				.AddField("triangleCount", &Meshlet::triangleCount)
				.AddField("boundingCenter", &Meshlet::boundingCenter)
				.AddField("boundingRadius", &Meshlet::boundingRadius);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddBone()
	{
		return AddLayout<Bone>("Bone", [](auto& layout) {
			layout.AddField("bindPose", &Bone::bindPose)
				.AddField("inverseBind", &Bone::inverseBind)
				.AddField("parent", &Bone::parent, c_InvalidIndex)
				.AddField("nameOffset", &Bone::nameOffset);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddAnimationClip()
	{
		return AddLayout<AnimationClip>("AnimationClip", [](auto& layout) {
			layout.AddField("nameOffset", &AnimationClip::nameOffset)
				.AddField("firstSample", &AnimationClip::firstSample)
				.AddField("frameCount", &AnimationClip::frameCount)
				.AddField("sampleRate", &AnimationClip::sampleRate, c_DefaultSampleRate)
				.AddField("duration", &AnimationClip::duration)
				.AddField("rootMotion", &AnimationClip::rootMotion)
				.AddField("locomotionSpeed", &AnimationClip::locomotionSpeed)
				.AddField("loop", &AnimationClip::loop);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddSourceStamp()
	{
		return AddLayout<SourceStamp>("SourceStamp", [](auto& layout) {
			layout.AddField("size", &SourceStamp::size).AddField("hash", &SourceStamp::hash);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddVatClip()
	{
		return AddLayout<VatClip>("VatClip", [](auto& layout) {
			layout.AddField("nameOffset", &VatClip::nameOffset)
				.AddField("firstRow", &VatClip::firstRow)
				.AddField("frameCount", &VatClip::frameCount)
				.AddField("firstPalette", &VatClip::firstPalette)
				.AddField("sampleRate", &VatClip::sampleRate)
				.AddField("duration", &VatClip::duration)
				.AddField("loop", &VatClip::loop);
		});
	}

	AssetSchemaBuilder&
	AssetSchemaBuilder::AddVatColumns()
	{
		return AddLayout<VatColumns>("VatColumns", [](auto& layout) {
			layout.AddField("columnBase", &VatColumns::columnBase)
				.AddField("vertexCount", &VatColumns::vertexCount);
		});
	}
}
