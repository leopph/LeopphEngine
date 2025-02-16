#ifndef MESH_SHADER_CORE_HLSLI
#define MESH_SHADER_CORE_HLSLI

#include "shader_interop.h"


uint3 UnpackTriangleIndices(uint const packed_indices) {
  return uint3(packed_indices & 0x3FF, (packed_indices >> 10) & 0x3FF, (packed_indices >> 20) & 0x3FF);
}


struct Meshlet {
  uint vertex_count;
  uint vertex_offset;
  uint primitive_count;
  uint primitive_offset;
};


template<typename VertexProcessor, typename PsIn>
void MeshShaderCore(
  uint const gid : SV_GroupID,
  uint const gtid : SV_GroupThreadID,
  uint const meshlet_buf_idx,
  uint const vertex_idx_buf_idx,
  uint const prim_idx_buf_idx,
  uint const draw_meshlet_offset,
  uint const draw_meshlet_count,
  uint const draw_instance_offset,
  uint const draw_instance_count,
  out PsIn out_verts[MESHLET_MAX_VERTS],
  out uint3 out_tris[MESHLET_MAX_PRIMS]) {
  uint const meshlet_idx = gid / draw_instance_count;
  StructuredBuffer<Meshlet> const meshlets = ResourceDescriptorHeap[meshlet_buf_idx];
  Meshlet const meshlet = meshlets[meshlet_idx + draw_meshlet_offset];

  uint start_instance = gid % draw_instance_count;
  uint instance_count = 1;

  if (meshlet_idx == draw_meshlet_count - 1) {
    uint const instances_per_group = min(MESHLET_MAX_VERTS / meshlet.vertex_count,
      MESHLET_MAX_PRIMS / meshlet.primitive_count);

    uint const unpacked_group_count = (draw_meshlet_count - 1) * draw_instance_count;
    uint const packed_index = gid - unpacked_group_count;

    start_instance = packed_index * instances_per_group;
    instance_count = min(draw_instance_count - start_instance, instances_per_group);
  }

  uint const vert_count = meshlet.vertex_count * instance_count;
  uint const prim_count = meshlet.primitive_count * instance_count;

  SetMeshOutputCounts(vert_count, prim_count);

  if (gtid < vert_count) {
    uint const read_index = gtid % meshlet.vertex_count;
    uint const instance_id = gtid / meshlet.vertex_count;

    StructuredBuffer<uint> const vertex_indices = ResourceDescriptorHeap[vertex_idx_buf_idx];
    uint const vertex_index = vertex_indices[meshlet.vertex_offset + read_index];
    uint const instance_index = start_instance + instance_id;

    out_verts[gtid] = VertexProcessor::CalculateVertex(vertex_index, instance_index);
  }

  for (uint i = 0; i < 2; i++) {
    uint const primitive_id = gtid + i * 128;

    if (primitive_id < prim_count) {
      uint const read_index = primitive_id % meshlet.primitive_count;
      uint const instance_id = primitive_id / meshlet.primitive_count;

      StructuredBuffer<uint> const primitive_indices = ResourceDescriptorHeap[prim_idx_buf_idx];

      out_tris[primitive_id] = UnpackTriangleIndices(primitive_indices[meshlet.primitive_offset + read_index]) + (
                                 meshlet.vertex_count * instance_id);
    }
  }
}


#define DECLARE_MESH_SHADER_MAIN(MainFuncName) [outputtopology("triangle")]\
[numthreads(MESHLET_MAX_VERTS, 1, 1)]\
void MainFuncName(\
  const uint gid : SV_GroupID, \
  const uint gtid : SV_GroupThreadID, \
  out vertices PsIn out_verts[MESHLET_MAX_VERTS], \
  out indices uint3 out_tris[MESHLET_MAX_PRIMS])

#endif
