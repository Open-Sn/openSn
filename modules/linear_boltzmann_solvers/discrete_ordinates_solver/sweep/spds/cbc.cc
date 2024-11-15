// SPDX-FileCopyrightText: 2024 The OpenSn Authors <https://open-sn.github.io/opensn/>
// SPDX-License-Identifier: MIT

#include "modules/linear_boltzmann_solvers/discrete_ordinates_solver/sweep/spds/cbc.h"
#include "framework/mesh/mesh_continuum/mesh_continuum.h"
#include "framework/logging/log.h"
#include "framework/utils/timer.h"
#include "framework/runtime.h"
#include "caliper/cali.h"
#include <boost/graph/topological_sort.hpp>

namespace opensn
{

CBC_SPDS::CBC_SPDS(const Vector3& omega, const MeshContinuum& grid, bool allow_cycles)
  : SPDS(omega, grid)
{
  CALI_CXX_MARK_SCOPE("CBC_SPDS::CBC_SPDS");

  size_t num_loc_cells = grid.local_cells.size();

  // Populate Cell Relationships
  std::vector<std::set<std::pair<int, double>>> cell_successors(num_loc_cells);
  std::set<int> location_successors;
  std::set<int> location_dependencies;

  PopulateCellRelationships(omega, location_dependencies, location_successors, cell_successors);

  location_successors_.reserve(location_successors.size());
  location_dependencies_.reserve(location_dependencies.size());

  for (auto v : location_successors)
    location_successors_.push_back(v);

  for (auto v : location_dependencies)
    location_dependencies_.push_back(v);

  // Build local cell graph
  Graph local_DG(num_loc_cells);

  // Create graph edges
  for (int c = 0; c < num_loc_cells; ++c)
    for (auto& successor : cell_successors[c])
      boost::add_edge(c, successor.first, successor.second, local_DG);

  if (allow_cycles)
  {
    auto edges_to_remove = RemoveCyclicDependencies(local_DG);
    for (auto& edge_to_remove : edges_to_remove)
      local_sweep_fas_.emplace_back(edge_to_remove.first, edge_to_remove.second);
  }

  // Generate topological sorting
  spls_.item_id.clear();
  boost::topological_sort(local_DG, std::back_inserter(spls_.item_id));
  std::reverse(spls_.item_id.begin(), spls_.item_id.end());
  if (spls_.item_id.empty())
  {
    throw std::logic_error("CBC_SPDS: Cyclic dependencies found in the local cell graph.\n"
                           "Cycles need to be allowed by the calling application.");
  }

  // Create task list
  std::vector<std::vector<int>> global_dependencies;
  global_dependencies.resize(opensn::mpi_comm.size());
  CommunicateLocationDependencies(location_dependencies_, global_dependencies);

  constexpr auto INCOMING = FaceOrientation::INCOMING;
  constexpr auto OUTGOING = FaceOrientation::OUTGOING;

  // For each local cell create a task
  for (const auto& cell : grid_.local_cells)
  {
    const size_t num_faces = cell.faces.size();
    unsigned int num_dependencies = 0;
    std::vector<uint64_t> succesors;

    for (size_t f = 0; f < num_faces; ++f)
    {
      if (cell_face_orientations_[cell.local_id][f] == INCOMING)
      {
        if (cell.faces[f].has_neighbor)
          ++num_dependencies;
      }
      else if (cell_face_orientations_[cell.local_id][f] == OUTGOING)
      {
        const auto& face = cell.faces[f];
        if (face.has_neighbor and grid.IsCellLocal(face.neighbor_id))
          succesors.push_back(grid.cells[face.neighbor_id].local_id);
      }
    }

    task_list_.push_back({num_dependencies, succesors, cell.local_id, &cell, false});
  }

  std::cerr << "omega = " << omega.PrintStr() << std::endl;
  std::cerr << "spls =";
  for (auto& v : spls_.item_id)
    std::cerr << " " << v;
  std::cerr << std::endl;
  std::cerr << "task_list:" << std::endl;
  for (auto& it : task_list_)
  {
    std::cerr << " - num_dependencies: " << it.num_dependencies << std::endl;
    std::cerr << "   reference_id: " << it.reference_id << std::endl;
    std::cerr << "   successors:";
    for (auto& s : it.successors)
      std::cerr << " " << s;
    std::cerr << std::endl;
  }
}

const std::vector<Task>&
CBC_SPDS::TaskList() const
{
  return task_list_;
}

} // namespace opensn
