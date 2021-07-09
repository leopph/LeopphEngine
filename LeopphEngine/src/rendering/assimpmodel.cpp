#include "assimpmodel.h"

#include "../instances/instanceholder.h"
#include "../util/logger.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/matrix4x4.h>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <queue>
#include <utility>

#include <iostream>

namespace leopph::impl
{
	AssimpModelImpl::AssimpModelImpl(std::filesystem::path path) :
		m_Path{ std::move(path) }
	{
		Assimp::Importer importer;
		const aiScene* scene{ importer.ReadFile(m_Path.string(), aiProcess_Triangulate | aiProcess_GenNormals)};

		if (scene == nullptr || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || scene->mRootNode == nullptr)
		{
			const auto errorMsg{ std::string{"Assimp error: "} + importer.GetErrorString() };
			Logger::Instance().Error(errorMsg);
			throw std::invalid_argument{ errorMsg };
		}

		m_Directory = m_Path.parent_path();

		struct NodeWithTrafo
		{
			aiNode* node;
			Matrix3 trafo;
		};

		std::queue<NodeWithTrafo> nodes;

		Matrix3 rootTrafo;
		for (unsigned i = 0; i < 3; ++i)
			for (unsigned j = 0; j < 3; ++j)
				rootTrafo[i][j] = scene->mRootNode->mTransformation[i][j];
		rootTrafo = Matrix3{ 1, 1, -1 } * rootTrafo * Matrix3{ 1, 1, -1 }.Inverse();
		nodes.emplace(scene->mRootNode, rootTrafo);

		while (!nodes.empty())
		{
			auto entry = nodes.front();
			nodes.pop();

			for (std::size_t i = 0; i < entry.node->mNumMeshes; ++i)
				m_Meshes.push_back(ProcessMesh(scene->mMeshes[entry.node->mMeshes[i]], scene, entry.trafo));

			for (std::size_t i = 0; i < entry.node->mNumChildren; ++i)
			{
				Matrix3 childTrafo;
				for (unsigned j = 0; j < 3; ++j)
					for (unsigned k = 0; k < 3; ++k)
						childTrafo[j][k] = entry.node->mChildren[i]->mTransformation[j][k];

				nodes.emplace(entry.node->mChildren[i], entry.trafo * childTrafo);
			}
		}
	}



	bool AssimpModelImpl::operator==(const AssimpModelImpl& other) const
	{
		return this->m_Directory == other.m_Directory;
	}



	void AssimpModelImpl::Draw(const Shader& shader, const std::vector<Matrix4>& modelMatrices, const std::vector<Matrix4>& normalMatrices) const
	{
		for (const auto & mesh : m_Meshes)
			mesh.Draw(shader, modelMatrices, normalMatrices);
	}



	const std::filesystem::path& AssimpModelImpl::Path() const
	{
		return m_Path;
	}



	Mesh AssimpModelImpl::ProcessMesh(aiMesh* mesh, const aiScene* scene, const Matrix3& trafo)
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned> indices;

		for (unsigned i = 0; i < mesh->mNumVertices; i++)
		{
			Vertex vertex;
			vertex.position = Vector3{ mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z } * trafo;
			vertex.normal = Vector3{ mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z } * trafo;

			// ONLY 1 UV LAYER SUPPORTED
			vertex.textureCoordinates = { 0.0f, 0.0f };
			for (std::size_t j = 0; j < AI_MAX_NUMBER_OF_TEXTURECOORDS; j++)
				if (mesh->HasTextureCoords(static_cast<unsigned>(j)))
				{
					vertex.textureCoordinates = { mesh->mTextureCoords[j][i].x, mesh->mTextureCoords[j][i].y };
					break;
				}
					
			vertices.push_back(vertex);
		}

		for (unsigned i = 0; i < mesh->mNumFaces; i++)
			for (unsigned j = 0; j < mesh->mFaces[i].mNumIndices; j++)
				indices.push_back(mesh->mFaces[i].mIndices[j]);


		aiMaterial* assimpMaterial{ scene->mMaterials[mesh->mMaterialIndex] };

		Material material;

		material.m_DiffuseTexture = LoadTexturesByType(assimpMaterial, aiTextureType_DIFFUSE);
		material.m_SpecularTexture = LoadTexturesByType(assimpMaterial, aiTextureType_SPECULAR);

		return Mesh(vertices, indices, std::move(material));
	}



	std::unique_ptr<Texture> AssimpModelImpl::LoadTexturesByType(aiMaterial* material, aiTextureType assimpType)
	{
		if (material->GetTextureCount(assimpType) > 0)
		{
			aiString location;
			material->GetTexture(assimpType, 0, &location);

			if (InstanceHolder::IsTextureStored(m_Directory / location.C_Str()))
			{
				Logger::Instance().Debug("Texture on path [" + (m_Directory / location.C_Str()).string() + "] requested from cache.");
				return InstanceHolder::CreateTexture(m_Directory / location.C_Str());
			}

			Logger::Instance().Debug("Texture on path [" + (m_Directory / location.C_Str()).string() + "] loaded from disk.");
			return std::make_unique<Texture>(m_Directory / location.C_Str());
		}

		Logger::Instance().Debug("Mesh contains no texture of the requested type.");
		return nullptr;
	}

	void AssimpModelImpl::OnReferringObjectsChanged(std::size_t newAmount) const
	{
		for (const auto& mesh : m_Meshes)
			mesh.OnReferringObjectsChanged(newAmount);
	}
}