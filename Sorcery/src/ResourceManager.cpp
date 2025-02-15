#include "ResourceManager.hpp"

#include <cassert>
#include <iostream>
#include <ranges>
#include <utility>

#include <DirectXTex.h>
#include <wrl/client.h>

#include "app.hpp"
#include "ExternalResource.hpp"
#include "FileIo.hpp"
#include "job_system.hpp"
#include "MemoryAllocation.hpp"
#include "Reflection.hpp"
#include "rendering/render_manager.hpp"
#include "Resources/Scene.hpp"

using Microsoft::WRL::ComPtr;


namespace sorcery {
namespace {
namespace {
std::vector const kQuadPositions{Vector3{-1, 1, 0}, Vector3{-1, -1, 0}, Vector3{1, -1, 0}, Vector3{1, 1, 0}};
std::vector const kQuadUvs{Vector2{0, 0}, Vector2{0, 1}, Vector2{1, 1}, Vector2{1, 0}};
std::vector<std::uint32_t> const kQuadIndices{2, 1, 0, 0, 3, 2};
std::vector const kCubePositions{
  Vector3{0.5f, 0.5f, 0.5f}, Vector3{0.5f, 0.5f, 0.5f}, Vector3{0.5f, 0.5f, 0.5f}, Vector3{-0.5f, 0.5f, 0.5f},
  Vector3{-0.5f, 0.5f, 0.5f}, Vector3{-0.5f, 0.5f, 0.5f}, Vector3{-0.5f, 0.5f, -0.5f}, Vector3{-0.5f, 0.5f, -0.5f},
  Vector3{-0.5f, 0.5f, -0.5f}, Vector3{0.5f, 0.5f, -0.5f}, Vector3{0.5f, 0.5f, -0.5f}, Vector3{0.5f, 0.5f, -0.5f},
  Vector3{0.5f, -0.5f, 0.5f}, Vector3{0.5f, -0.5f, 0.5f}, Vector3{0.5f, -0.5f, 0.5f}, Vector3{-0.5f, -0.5f, 0.5f},
  Vector3{-0.5f, -0.5f, 0.5f}, Vector3{-0.5f, -0.5f, 0.5f}, Vector3{-0.5f, -0.5f, -0.5f}, Vector3{-0.5f, -0.5f, -0.5f},
  Vector3{-0.5f, -0.5f, -0.5f}, Vector3{0.5f, -0.5f, -0.5f}, Vector3{0.5f, -0.5f, -0.5f}, Vector3{0.5f, -0.5f, -0.5f},
};
std::vector const kCubeUvs{
  Vector2{1, 0}, Vector2{1, 0}, Vector2{0, 0}, Vector2{0, 0}, Vector2{0, 0}, Vector2{1, 0}, Vector2{1, 0},
  Vector2{0, 1}, Vector2{0, 0}, Vector2{0, 0}, Vector2{1, 1}, Vector2{1, 0}, Vector2{1, 1}, Vector2{1, 1},
  Vector2{0, 1}, Vector2{0, 1}, Vector2{0, 1}, Vector2{1, 1}, Vector2{1, 1}, Vector2{0, 0}, Vector2{0, 1},
  Vector2{0, 1}, Vector2{1, 0}, Vector2{1, 1}
};
std::vector<std::uint32_t> const kCubeIndices{
  // Top face
  7, 4, 1, 1, 10, 7,
  // Bottom face
  16, 19, 22, 22, 13, 16,
  // Front face
  23, 20, 8, 8, 11, 23,
  // Back face
  17, 14, 2, 2, 5, 17,
  // Right face
  21, 9, 0, 0, 12, 21,
  // Left face
  15, 3, 6, 6, 18, 15
};
}
}


auto ResourceManager::ResourceGuidLess::operator()(std::unique_ptr<Resource> const& lhs,
                                                   std::unique_ptr<Resource> const& rhs) const noexcept -> bool {
  return lhs->GetGuid() < rhs->GetGuid();
}


auto ResourceManager::ResourceGuidLess::operator()(std::unique_ptr<Resource> const& lhs,
                                                   Guid const& rhs) const noexcept -> bool {
  return lhs->GetGuid() < rhs;
}


auto ResourceManager::ResourceGuidLess::operator()(Guid const& lhs,
                                                   std::unique_ptr<Resource> const& rhs) const noexcept -> bool {
  return lhs < rhs->GetGuid();
}


auto ResourceManager::InternalLoadResource(Guid const& guid, ResourceDescription const& desc) -> ObserverPtr<Resource> {
  ObserverPtr<Job> loader_job;

  std::cout << "loading " << desc.name << '\n';

  {
    auto loader_jobs{loader_jobs_.Lock()};

    if (auto const it{loader_jobs->find(guid)}; it != loader_jobs->end()) {
      loader_job = it->second;
    } else {
      loader_job = job_system_->CreateJob([this, &guid, &desc] {
        std::unique_ptr<Resource> res;

        if (desc.pathAbs.extension() == EXTERNAL_RESOURCE_EXT) {
          std::vector<std::uint8_t> fileBytes;

          if (!ReadFileBinary(desc.pathAbs, fileBytes)) {
            return;
          }

          ExternalResourceCategory resCat;
          std::vector<std::byte> resBytes;

          if (!UnpackExternalResource(as_bytes(std::span{fileBytes}), resCat, resBytes)) {
            return;
          }

          switch (resCat) {
            case ExternalResourceCategory::Texture: {
              res = LoadTexture(resBytes);
              break;
            }

            case ExternalResourceCategory::Mesh: {
              res = LoadMesh(resBytes);
              break;
            }
          }
        } else if (desc.pathAbs.extension() == SCENE_RESOURCE_EXT) {
          res = CreateDeserialize<Scene>(YAML::LoadFile(desc.pathAbs.string()));
        } else if (desc.pathAbs.extension() == MATERIAL_RESOURCE_EXT) {
          res = CreateDeserialize<Material>(YAML::LoadFile(desc.pathAbs.string()));
        }

        if (res) {
          res->SetGuid(guid);
          res->SetName(desc.name);

          auto const [it, inserted]{loaded_resources_.Lock()->emplace(std::move(res))};
          assert(inserted);
        }
      });
      job_system_->Run(loader_job);
      loader_jobs->emplace(guid, loader_job);
    }
  }

  assert(loader_job);
  job_system_->Wait(loader_job);

  {
    loader_jobs_.Lock()->erase(guid);
  }

  {
    auto const resources{loaded_resources_.LockShared()};
    auto const it{resources->find(guid)};
    return ObserverPtr{it != resources->end() ? it->get() : nullptr};
  }
}


auto ResourceManager::LoadTexture(
  std::span<std::byte const> const bytes) noexcept -> MaybeNull<std::unique_ptr<Resource>> {
  DirectX::TexMetadata meta;
  DirectX::ScratchImage img;
  if (FAILED(LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &meta, img))) {
    return nullptr;
  }

  auto tex{App::Instance().GetRenderManager().CreateReadOnlyTexture(img)};

  if (!tex) {
    return nullptr;
  }

  std::unique_ptr<Resource> ret;

  if (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE2D) {
    if (meta.IsCubemap()) {
      ret = Create<Cubemap>(std::move(tex));
    } else {
      ret = Create<Texture2D>(std::move(tex));
    }
  } else {
    return nullptr;
  }

  return ret;
}


auto ResourceManager::LoadMesh(std::span<std::byte const> const bytes) -> MaybeNull<std::unique_ptr<Resource>> {
  auto cur_bytes{as_bytes(std::span{bytes})};

  // Element counts

  std::uint64_t material_slot_count;

  if (!DeserializeFromBinary(cur_bytes, material_slot_count)) {
    return nullptr;
  }

  cur_bytes = cur_bytes.subspan(sizeof material_slot_count);
  std::uint64_t submesh_count;

  if (!DeserializeFromBinary(cur_bytes, submesh_count)) {
    return nullptr;
  }

  cur_bytes = cur_bytes.subspan(sizeof submesh_count);
  std::uint64_t anim_count;

  if (!DeserializeFromBinary(cur_bytes, anim_count)) {
    return nullptr;
  }

  cur_bytes = cur_bytes.subspan(sizeof anim_count);
  std::uint64_t skeleton_size;

  if (!DeserializeFromBinary(cur_bytes, skeleton_size)) {
    return nullptr;
  }

  cur_bytes = cur_bytes.subspan(sizeof skeleton_size);
  std::uint64_t bone_count;

  if (!DeserializeFromBinary(cur_bytes, bone_count)) {
    return nullptr;
  }

  cur_bytes = cur_bytes.subspan(sizeof bone_count);
  mesh_data mesh_data;

  // Material slots

  mesh_data.material_slots.resize(material_slot_count);

  for (auto i{0ull}; i < material_slot_count; i++) {
    if (!DeserializeFromBinary(cur_bytes, mesh_data.material_slots[i].name)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(mesh_data.material_slots[i].name.size() + 8);
  }

  // Submeshes

  mesh_data.submeshes.resize(submesh_count);

  for (auto i{0ull}; i < submesh_count; i++) {
    std::uint64_t vert_count;

    if (!DeserializeFromBinary(cur_bytes, vert_count)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof vert_count);
    std::uint64_t meshlet_count;

    if (!DeserializeFromBinary(cur_bytes, meshlet_count)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof meshlet_count);
    std::uint64_t vtx_idx_count;

    if (!DeserializeFromBinary(cur_bytes, vtx_idx_count)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof vtx_idx_count);
    std::uint64_t prim_idx_count;

    if (!DeserializeFromBinary(cur_bytes, prim_idx_count)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof prim_idx_count);

    mesh_data.submeshes[i].positions.resize(vert_count);
    std::memcpy(mesh_data.submeshes[i].positions.data(), cur_bytes.data(), vert_count * sizeof(Vector3));
    cur_bytes = cur_bytes.subspan(vert_count * sizeof(Vector3));

    mesh_data.submeshes[i].normals.resize(vert_count);
    std::memcpy(mesh_data.submeshes[i].normals.data(), cur_bytes.data(), vert_count * sizeof(Vector3));
    cur_bytes = cur_bytes.subspan(vert_count * sizeof(Vector3));

    mesh_data.submeshes[i].tangents.resize(vert_count);
    std::memcpy(mesh_data.submeshes[i].tangents.data(), cur_bytes.data(), vert_count * sizeof(Vector3));
    cur_bytes = cur_bytes.subspan(vert_count * sizeof(Vector3));

    mesh_data.submeshes[i].uvs.resize(vert_count);
    std::memcpy(mesh_data.submeshes[i].uvs.data(), cur_bytes.data(), vert_count * sizeof(Vector2));
    cur_bytes = cur_bytes.subspan(vert_count * sizeof(Vector2));

    mesh_data.submeshes[i].bone_weights.resize(vert_count);
    std::memcpy(mesh_data.submeshes[i].bone_weights.data(), cur_bytes.data(), vert_count * sizeof(Vector4));
    cur_bytes = cur_bytes.subspan(vert_count * sizeof(Vector4));

    mesh_data.submeshes[i].bone_indices.resize(vert_count);
    std::memcpy(mesh_data.submeshes[i].bone_indices.data(), cur_bytes.data(),
      vert_count * sizeof(Vector<std::uint32_t, 4>));
    cur_bytes = cur_bytes.subspan(vert_count * sizeof(Vector<std::uint32_t, 4>));

    mesh_data.submeshes[i].meshlets.resize(meshlet_count);
    std::memcpy(mesh_data.submeshes[i].meshlets.data(), cur_bytes.data(), meshlet_count * sizeof(MeshletData));
    cur_bytes = cur_bytes.subspan(meshlet_count * sizeof(MeshletData));

    mesh_data.submeshes[i].vertex_indices.resize(vtx_idx_count);
    std::memcpy(mesh_data.submeshes[i].vertex_indices.data(), cur_bytes.data(), vtx_idx_count);
    cur_bytes = cur_bytes.subspan(vtx_idx_count);

    mesh_data.submeshes[i].triangle_indices.resize(prim_idx_count);
    std::memcpy(mesh_data.submeshes[i].triangle_indices.data(), cur_bytes.data(),
      prim_idx_count * sizeof(MeshletTriangleData));
    cur_bytes = cur_bytes.subspan(prim_idx_count * sizeof(MeshletTriangleData));

    if (!DeserializeFromBinary(cur_bytes, mesh_data.submeshes[i].material_idx)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof(std::uint32_t));

    if (!DeserializeFromBinary(cur_bytes, mesh_data.submeshes[i].idx32)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof(bool));
  }

  // Animations

  mesh_data.animations.resize(anim_count);

  for (auto i{0ull}; i < anim_count; i++) {
    if (!DeserializeFromBinary(cur_bytes, mesh_data.animations[i].name)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(mesh_data.animations[i].name.size() + 8);

    if (!DeserializeFromBinary(cur_bytes, mesh_data.animations[i].duration)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof(float));

    if (!DeserializeFromBinary(cur_bytes, mesh_data.animations[i].ticks_per_second)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof(float));
    std::uint64_t node_anim_count;

    if (!DeserializeFromBinary(cur_bytes, node_anim_count)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof node_anim_count);
    mesh_data.animations[i].node_anims.resize(node_anim_count);

    for (auto j{0ull}; j < node_anim_count; j++) {
      if (!DeserializeFromBinary(cur_bytes, mesh_data.animations[i].node_anims[j].node_idx)) {
        return nullptr;
      }

      cur_bytes = cur_bytes.subspan(sizeof(std::uint32_t));
      std::uint64_t pos_key_count;

      if (!DeserializeFromBinary(cur_bytes, pos_key_count)) {
        return nullptr;
      }

      cur_bytes = cur_bytes.subspan(sizeof pos_key_count);
      std::uint64_t rot_key_count;

      if (!DeserializeFromBinary(cur_bytes, rot_key_count)) {
        return nullptr;
      }

      cur_bytes = cur_bytes.subspan(sizeof rot_key_count);
      std::uint64_t scale_key_count;

      if (!DeserializeFromBinary(cur_bytes, scale_key_count)) {
        return nullptr;
      }

      cur_bytes = cur_bytes.subspan(sizeof scale_key_count);

      mesh_data.animations[i].node_anims[j].position_keys.resize(pos_key_count);
      std::memcpy(mesh_data.animations[i].node_anims[j].position_keys.data(), cur_bytes.data(),
        pos_key_count * sizeof(AnimPositionKey));
      cur_bytes = cur_bytes.subspan(pos_key_count * sizeof(AnimPositionKey));


      mesh_data.animations[i].node_anims[j].rotation_keys.resize(rot_key_count);
      std::memcpy(mesh_data.animations[i].node_anims[j].rotation_keys.data(), cur_bytes.data(),
        rot_key_count * sizeof(AnimRotationKey));
      cur_bytes = cur_bytes.subspan(rot_key_count * sizeof(AnimRotationKey));

      mesh_data.animations[i].node_anims[j].scaling_keys.resize(scale_key_count);
      std::memcpy(mesh_data.animations[i].node_anims[j].scaling_keys.data(), cur_bytes.data(),
        scale_key_count * sizeof(AnimScalingKey));
      cur_bytes = cur_bytes.subspan(scale_key_count * sizeof(AnimScalingKey));
    }
  }

  // Skeleton nodes

  mesh_data.skeleton.resize(skeleton_size);

  for (auto i{0ull}; i < skeleton_size; i++) {
    if (!DeserializeFromBinary(cur_bytes, mesh_data.skeleton[i].name)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(mesh_data.skeleton[i].name.size() + 8);
    bool has_parent;

    if (!DeserializeFromBinary(cur_bytes, has_parent)) {
      return nullptr;
    }

    cur_bytes = cur_bytes.subspan(sizeof has_parent);

    if (has_parent) {
      std::uint32_t parent_idx;

      if (!DeserializeFromBinary(cur_bytes, parent_idx)) {
        return nullptr;
      }

      mesh_data.skeleton[i].parent_idx = parent_idx;
      cur_bytes = cur_bytes.subspan(sizeof std::uint32_t);
    }

    std::memcpy(mesh_data.skeleton[i].transform.GetData(), cur_bytes.data(), sizeof(Matrix4));
    cur_bytes = cur_bytes.subspan(sizeof Matrix4);
  }

  // Bones

  mesh_data.bones.resize(bone_count);
  std::memcpy(mesh_data.bones.data(), cur_bytes.data(), bone_count * sizeof(Bone));
  cur_bytes = cur_bytes.subspan(bone_count * sizeof(Bone));

  assert(cur_bytes.empty());

  return Create<Mesh>(std::move(mesh_data));
}


ResourceManager::ResourceManager(JobSystem& job_system) :
  job_system_{&job_system} {}


auto ResourceManager::Unload(Guid const& guid) -> void {
  auto resources{loaded_resources_.Lock()};

  if (auto const it{resources->find(guid)}; it != std::end(*resources)) {
    resources->erase(it);
  }
}


auto ResourceManager::UnloadAll() -> void {
  loaded_resources_.Lock()->clear();
}


auto ResourceManager::IsLoaded(Guid const& guid) -> bool {
  for (auto const& res : default_resources_) {
    if (res->GetGuid() == guid) {
      return true;
    }
  }

  return loaded_resources_.LockShared()->contains(guid);
}


auto ResourceManager::UpdateMappings(std::map<Guid, ResourceDescription> mappings) -> void {
  while (true) {
    if (auto self_mappings{mappings_.TryLock()}) {
      **self_mappings = std::move(mappings);
      break;
    }
  }
}


auto ResourceManager::GetGuidsForResourcesOfType(rttr::type const& type,
                                                 std::vector<Guid>& out) noexcept -> void {
  // Default resources
  for (auto const& res : default_resources_) {
    if (rttr::type::get(*res).is_derived_from(type)) {
      out.emplace_back(res->GetGuid());
    }
  }

  // File mappings
  for (auto const& [guid, desc] : *mappings_.LockShared()) {
    if (desc.type.is_derived_from(type)) {
      out.emplace_back(guid);
    }
  }

  // Other, loaded resources that don't come from files
  for (auto const& res : *loaded_resources_.LockShared()) {
    auto contains{false};
    for (auto const& guid : out) {
      if (guid == res->GetGuid()) {
        contains = true;
        break;
      }
    }

    if (!contains && rttr::type::get(*res).is_derived_from(type)) {
      out.emplace_back(res->GetGuid());
    }
  }
}


auto ResourceManager::GetInfoForResourcesOfType(rttr::type const& type, std::vector<ResourceInfo>& out) -> void {
  // Default resources
  for (auto const& res : default_resources_) {
    if (auto const res_type{rttr::type::get(*res)}; res_type.is_derived_from(type)) {
      out.emplace_back(res->GetGuid(), res->GetName(), res_type);
    }
  }

  // File mappings
  for (auto const& [guid, desc] : *mappings_.LockShared()) {
    if (desc.type.is_derived_from(type)) {
      out.emplace_back(guid, desc.name, desc.type);
    }
  }

  // Other, loaded resources that don't come from files
  for (auto const& res : *loaded_resources_.LockShared()) {
    auto contains{false};
    for (auto const& res_info : out) {
      if (res_info.guid == res->GetGuid()) {
        contains = true;
        break;
      }
    }

    if (!contains && rttr::type::get(*res).is_derived_from(type)) {
      out.emplace_back(res->GetGuid(), res->GetName(), res->get_type());
    }
  }
}


auto ResourceManager::GetDefaultMaterial() const noexcept -> ObserverPtr<Material> {
  return ObserverPtr{default_mtl_.get()};
}


auto ResourceManager::GetCubeMesh() const noexcept -> ObserverPtr<Mesh> {
  return ObserverPtr{cube_mesh_.get()};
}


auto ResourceManager::GetPlaneMesh() const noexcept -> ObserverPtr<Mesh> {
  return ObserverPtr{plane_mesh_.get()};
}


auto ResourceManager::GetSphereMesh() const noexcept -> ObserverPtr<Mesh> {
  return ObserverPtr{sphere_mesh_.get()};
}


auto ResourceManager::CreateDefaultResources() -> void {
  if (!default_mtl_) {
    default_mtl_ = Create<Material>();
    default_mtl_->SetGuid(default_mtl_guid_);
    default_mtl_->SetName("Default Material");
    default_resources_.emplace_back(default_mtl_.get());
  }

  if (!cube_mesh_) {
    mesh_data cube_data;

    cube_data.material_slots.emplace_back("Material");
    cube_data.submeshes.resize(1);

    cube_data.submeshes[0].positions = kCubePositions;
    CalculateNormals(kCubePositions, kCubeIndices, cube_data.submeshes[0].normals);
    CalculateTangents(kCubePositions, kCubeUvs, kCubeIndices, cube_data.submeshes[0].tangents);
    cube_data.submeshes[0].uvs = kCubeUvs;
    cube_data.submeshes[0].material_idx = 0;
    cube_data.submeshes[0].idx32 = true;

    if (!ComputeMeshlets(kCubeIndices, kCubePositions, cube_data.submeshes[0].meshlets,
      cube_data.submeshes[0].vertex_indices, cube_data.submeshes[0].triangle_indices)) {
      throw std::runtime_error{"Failed to compute meshlets for default cube mesh."};
    }

    cube_mesh_ = Create<Mesh>(cube_data);
    cube_mesh_->SetGuid(cube_mesh_guid_);
    cube_mesh_->SetName("Cube");
    default_resources_.emplace_back(cube_mesh_.get());
  }

  if (!plane_mesh_) {
    mesh_data plane_data;

    plane_data.material_slots.emplace_back("Material");
    plane_data.submeshes.resize(1);

    plane_data.submeshes[0].positions = kQuadPositions;
    CalculateNormals(kQuadPositions, kQuadIndices, plane_data.submeshes[0].normals);
    CalculateTangents(kQuadPositions, kQuadUvs, kQuadIndices, plane_data.submeshes[0].tangents);
    plane_data.submeshes[0].uvs = kQuadUvs;
    plane_data.submeshes[0].material_idx = 0;
    plane_data.submeshes[0].idx32 = true;

    if (!ComputeMeshlets(kQuadIndices, kQuadPositions, plane_data.submeshes[0].meshlets,
      plane_data.submeshes[0].vertex_indices, plane_data.submeshes[0].triangle_indices)) {
      throw std::runtime_error{"Failed to compute meshlets for default plane mesh."};
    }

    plane_mesh_ = Create<Mesh>(plane_data);
    plane_mesh_->SetGuid(plane_mesh_guid_);
    plane_mesh_->SetName("Plane");
    default_resources_.emplace_back(plane_mesh_.get());
  }

  if (!sphere_mesh_) {
    mesh_data sphere_data;

    sphere_data.material_slots.emplace_back("Material");
    sphere_data.submeshes.resize(1);

    std::vector<std::uint32_t> sphere_indices;

    rendering::GenerateSphereMesh(1, 50, 50, sphere_data.submeshes[0].positions, sphere_data.submeshes[0].normals,
      sphere_data.submeshes[0].uvs, sphere_indices);
    CalculateTangents(sphere_data.submeshes[0].positions, sphere_data.submeshes[0].uvs, sphere_indices,
      sphere_data.submeshes[0].tangents);
    sphere_data.submeshes[0].material_idx = 0;
    sphere_data.submeshes[0].idx32 = true;

    if (!ComputeMeshlets(sphere_indices, sphere_data.submeshes[0].positions, sphere_data.submeshes[0].meshlets,
      sphere_data.submeshes[0].vertex_indices, sphere_data.submeshes[0].triangle_indices)) {
      throw std::runtime_error{"Failed to compute meshlets for default sphere mesh."};
    }

    sphere_mesh_ = Create<Mesh>(sphere_data);
    sphere_mesh_->SetGuid(sphere_mesh_guid_);
    sphere_mesh_->SetName("Sphere");
    default_resources_.emplace_back(sphere_mesh_.get());
  }
}
}
