#include "framework/mesh/volume_mesher/volume_mesher.h"
#include "framework/mesh/mesh_continuum/mesh_continuum.h"
#include "framework/mesh/unpartitioned_mesh/unpartitioned_mesh.h"
#include "framework/mesh/logical_volume/logical_volume.h"
#include "framework/utils/timer.h"
#include "framework/runtime.h"
#include "framework/logging/log.h"

namespace opensn
{

VolumeMesher::VolumeMesher(VolumeMesherType type) : type_(type)
{
}

void
VolumeMesher::SetContinuum(std::shared_ptr<MeshContinuum>& grid)
{
  grid_ptr_ = grid;
}

std::shared_ptr<MeshContinuum>&
VolumeMesher::GetContinuum()
{
  return grid_ptr_;
}

VolumeMesherType
VolumeMesher::Type() const
{
  return type_;
}

void
VolumeMesher::SetMatIDFromLogical(const LogicalVolume& log_vol, bool sense, int mat_id)
{
  log.Log0Verbose1() << program_timer.GetTimeString()
                     << " Setting material id from logical volume.";
  std::shared_ptr<MeshContinuum> vol_cont = GetCurrentMesh();

  int num_cells_modified = 0;
  for (auto& cell : vol_cont->local_cells)
  {
    if (log_vol.Inside(cell.centroid_) and sense)
    {
      cell.material_id_ = mat_id;
      ++num_cells_modified;
    }
  }

  const auto& ghost_ids = vol_cont->cells.GetGhostGlobalIDs();
  for (uint64_t ghost_id : ghost_ids)
  {
    auto& cell = vol_cont->cells[ghost_id];
    if (log_vol.Inside(cell.centroid_) and sense)
      cell.material_id_ = mat_id;
  }

  int global_num_cells_modified;
  mpi_comm.all_reduce(num_cells_modified, global_num_cells_modified, mpi::op::sum<int>());

  log.Log0Verbose1() << program_timer.GetTimeString()
                     << " Done setting material id from logical volume. "
                     << "Number of cells modified = " << global_num_cells_modified << ".";
}

void
VolumeMesher::SetBndryIDFromLogical(const LogicalVolume& log_vol,
                                    bool sense,
                                    const std::string& bndry_name)
{
  log.Log() << program_timer.GetTimeString() << " Setting boundary id from logical volume.";
  std::shared_ptr<MeshContinuum> vol_cont = GetCurrentMesh();

  // Check if name already has id
  auto& grid_bndry_id_map = vol_cont->GetBoundaryIDMap();
  uint64_t bndry_id = vol_cont->MakeBoundaryID(bndry_name);

  // Loop over cells
  int num_faces_modified = 0;
  for (auto& cell : vol_cont->local_cells)
  {
    for (auto& face : cell.faces_)
    {
      if (face.has_neighbor_)
        continue;
      if (log_vol.Inside(face.centroid_) and sense)
      {
        face.neighbor_id_ = bndry_id;
        ++num_faces_modified;
      }
    }
  }

  int global_num_faces_modified;
  mpi_comm.all_reduce(num_faces_modified, global_num_faces_modified, mpi::op::sum<int>());

  if (global_num_faces_modified > 0 and grid_bndry_id_map.count(bndry_id) == 0)
    grid_bndry_id_map[bndry_id] = bndry_name;

  log.Log() << program_timer.GetTimeString() << " Done setting boundary id from logical volume. "
            << "Number of faces modified = " << global_num_faces_modified << ".";
}

} // namespace opensn
