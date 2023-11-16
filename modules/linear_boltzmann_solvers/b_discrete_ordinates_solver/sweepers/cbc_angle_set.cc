#include "modules/linear_boltzmann_solvers/b_discrete_ordinates_solver/sweepers/cbc_angle_set.h"

#include "modules/linear_boltzmann_solvers/b_discrete_ordinates_solver/sweepers/cbc_async_comm.h"
#include "modules/linear_boltzmann_solvers/b_discrete_ordinates_solver/sweepers/cbc_spds.h"
#include "framework/mesh/sweep_utilities/sweep_chunk_base.h"
#include "framework/mesh/mesh_continuum/mesh_continuum.h"
#include "framework/math/math_range.h"

#include "framework/runtime.h"
#include "framework/logging/log.h"

namespace opensn
{
namespace lbs
{

CBC_AngleSet::CBC_AngleSet(size_t id,
                           size_t num_groups,
                           const SPDS& spds,
                           std::shared_ptr<FLUDS>& fluds,
                           const std::vector<size_t>& angle_indices,
                           std::map<uint64_t, SweepBndryPtr>& sim_boundaries,
                           size_t in_ref_subset,
                           const ChiMPICommunicatorSet& comm_set)
  : AngleSet(id, num_groups, spds, fluds, angle_indices, sim_boundaries, in_ref_subset),
    cbc_spds_(dynamic_cast<const CBC_SPDS&>(spds_)),
    async_comm_(id, *fluds, comm_set)
{
}

AsynchronousCommunicator*
CBC_AngleSet::GetCommunicator()
{
  return static_cast<AsynchronousCommunicator*>(&async_comm_);
}

AngleSetStatus
CBC_AngleSet::AngleSetAdvance(SweepChunk& sweep_chunk,
                              const std::vector<size_t>& timing_tags,
                              ExecutionPermission permission)
{
  typedef AngleSetStatus Status;

  if (executed_) return Status::FINISHED;

  if (current_task_list_.empty()) current_task_list_ = cbc_spds_.TaskList();

  sweep_chunk.SetAngleSet(*this);

  auto tasks_who_received_data = async_comm_.ReceiveData();

  for (const uint64_t task_number : tasks_who_received_data)
    --current_task_list_[task_number].num_dependencies_;

  async_comm_.SendData();

  // Check if boundaries allow for execution
  for (auto& [bid, bndry] : ref_boundaries_)
    if (not bndry->CheckAnglesReadyStatus(angles_, ref_group_subset_)) return Status::NOT_FINISHED;

  bool all_tasks_completed = true;
  bool a_task_executed = true;
  while (a_task_executed)
  {
    a_task_executed = false;
    for (auto& cell_task : current_task_list_)
    {
      if (not cell_task.completed_) all_tasks_completed = false;
      if (cell_task.num_dependencies_ == 0 and not cell_task.completed_)
      {
        log.LogEvent(timing_tags[0], Logger::EventType::EVENT_BEGIN);
        sweep_chunk.SetCell(cell_task.cell_ptr_, *this);
        sweep_chunk.Sweep(*this);

        for (uint64_t local_task_num : cell_task.successors_)
          --current_task_list_[local_task_num].num_dependencies_;
        log.LogEvent(timing_tags[0], Logger::EventType::EVENT_END);

        cell_task.completed_ = true;
        a_task_executed = true;
        async_comm_.SendData();
      }
    } // for cell_task
    async_comm_.SendData();
  }
  // for (auto& cell_task : current_task_list_)
  //{
  //   if (not cell_task.completed_) all_tasks_completed = false;
  //   if (cell_task.num_dependencies_ == 0 and not cell_task.completed_)
  //   {
  //     log.LogEvent(timing_tags[0], chi::Logger::EventType::EVENT_BEGIN);
  //     sweep_chunk.SetCell(cell_task.cell_ptr_, *this);
  //     sweep_chunk.Sweep(*this);
  //
  //     for (uint64_t local_task_num : cell_task.successors_)
  //       --current_task_list_[local_task_num].num_dependencies_;
  //     log.LogEvent(timing_tags[0], chi::Logger::EventType::EVENT_END);
  //
  //     cell_task.completed_ = true;
  //     async_comm_.SendData();
  //   }
  // } // for cell_task

  const bool all_messages_sent = async_comm_.SendData();

  if (all_tasks_completed and all_messages_sent)
  {
    // Update boundary readiness
    for (auto& [bid, bndry] : ref_boundaries_)
      bndry->UpdateAnglesReadyStatus(angles_, ref_group_subset_);
    executed_ = true;
    return Status::FINISHED;
  }

  return Status::NOT_FINISHED;
}

void
CBC_AngleSet::ResetSweepBuffers()
{
  current_task_list_.clear();
  async_comm_.Reset();
  fluds_->ClearLocalAndReceivePsi();
  executed_ = false;
}

const double*
CBC_AngleSet::PsiBndry(uint64_t bndry_map,
                       unsigned int angle_num,
                       uint64_t cell_local_id,
                       unsigned int face_num,
                       unsigned int fi,
                       int g,
                       size_t gs_ss_begin,
                       bool surface_source_active)
{
  if (ref_boundaries_[bndry_map]->IsReflecting())
    return ref_boundaries_[bndry_map]->HeterogeneousPsiIncoming(
      cell_local_id, face_num, fi, angle_num, g, gs_ss_begin);

  if (not surface_source_active) return ref_boundaries_[bndry_map]->ZeroFlux(g);

  return ref_boundaries_[bndry_map]->HeterogeneousPsiIncoming(
    cell_local_id, face_num, fi, angle_num, g, gs_ss_begin);
}

double*
CBC_AngleSet::ReflectingPsiOutBoundBndry(uint64_t bndry_map,
                                         unsigned int angle_num,
                                         uint64_t cell_local_id,
                                         unsigned int face_num,
                                         unsigned int fi,
                                         size_t gs_ss_begin)
{
  return ref_boundaries_[bndry_map]->HeterogeneousPsiOutgoing(
    cell_local_id, face_num, fi, angle_num, gs_ss_begin);
}

} // namespace lbs
} // namespace opensn
