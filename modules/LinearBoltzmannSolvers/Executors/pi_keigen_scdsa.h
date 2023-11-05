#pragma once

#include "pi_keigen.h"
#include "LinearBoltzmannSolvers/A_LBSSolver/Acceleration/diffusion.h"

namespace chi_math
{
class VectorGhostCommunicator;
}

namespace lbs
{

class XXPowerIterationKEigenSCDSA : public XXPowerIterationKEigen
{
  typedef std::shared_ptr<acceleration::DiffusionSolver> DiffusionSolverPtr;
  typedef std::shared_ptr<chi_math::VectorGhostCommunicator> VecGhostCommPtr;
  typedef std::shared_ptr<chi_math::SpatialDiscretization> SDMPtr;

protected:
  int accel_pi_max_its_;
  double accel_pi_k_tol_;
  bool accel_pi_verbose_;
  DiffusionSolverPtr diffusion_solver_ = nullptr;

  const std::string diffusion_solver_sdm_;

  SDMPtr continuous_sdm_ptr_ = nullptr;
  bool requires_ghosts_ = false;
  struct GhostInfo
  {
    VecGhostCommPtr vector_ghost_communicator = nullptr;
    std::map<int64_t, int64_t> ghost_global_id_2_local_map;
  };
  GhostInfo lbs_pwld_ghost_info_;

  double diff_accel_diffusion_l_abs_tol_;
  int diff_accel_diffusion_max_iters_;
  bool diff_accel_diffusion_verbose_;
  std::string diff_accel_diffusion_petsc_options_;

public:
  static chi::InputParameters GetInputParameters();
  explicit XXPowerIterationKEigenSCDSA(const chi::InputParameters& params);

  void Initialize() override;
  void Execute() override;

  /**
   * Copies only the scalar moments from an lbs primary flux moments vector.
   */
  std::vector<double> CopyOnlyPhi0(const LBSGroupset& groupset, const std::vector<double>& phi_in);

  /**
   * Copies back only the scalar moments to a lbs primary flux vector.
   */
  void ProjectBackPhi0(const LBSGroupset& groupset,
                       const std::vector<double>& input,
                       std::vector<double>& output);

  /**
   * Creates a ghost communicator and all associated information.
   */
  GhostInfo MakePWLDVecGhostCommInfo(const chi_math::SpatialDiscretization& sdm,
                                     const chi_math::UnknownManager& uk_man);

  /**
   * This method takes an input vector that is the local version of a PWLD discrete space and then
   * makes it continuous by applying nodal averages.
   */
  static std::vector<double>
  NodallyAveragedPWLDVector(const std::vector<double>& input,
                            const chi_math::SpatialDiscretization& pwld_sdm,
                            const chi_math::SpatialDiscretization& pwlc_sdm,
                            const chi_math::UnknownManager& uk_man,
                            const XXPowerIterationKEigenSCDSA::GhostInfo& ghost_info);
};

} // namespace lbs
