#pragma once

#include "framework/mesh/sweep_utilities/spds/spds.h"

#include "framework/mesh/sweep_utilities/sweep_namespace.h"

namespace lbs
{

/**Cell-by-Cell (CBC) Sweep Plane Data Structure*/
class CBC_SPDS : public chi_mesh::sweep_management::SPDS
{
public:
  CBC_SPDS(const chi_mesh::Vector3& omega,
           const chi_mesh::MeshContinuum& grid,
           bool cycle_allowance_flag,
           bool verbose);

  const std::vector<chi_mesh::sweep_management::Task>& TaskList() const;

protected:
  std::vector<chi_mesh::sweep_management::Task> task_list_;
};

} // namespace lbs