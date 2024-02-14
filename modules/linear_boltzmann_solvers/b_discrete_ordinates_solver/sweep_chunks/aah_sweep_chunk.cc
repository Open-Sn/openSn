#include "modules/linear_boltzmann_solvers/b_discrete_ordinates_solver/sweep_chunks/aah_sweep_chunk.h"

#include "framework/mesh/mesh_continuum/mesh_continuum.h"
#include "framework/mesh/sweep_utilities/fluds/aah_fluds.h"

namespace opensn
{
namespace lbs
{

AAH_SweepChunk::AAH_SweepChunk(const MeshContinuum& grid,
                               const SpatialDiscretization& discretization,
                               const std::vector<UnitCellMatrices>& unit_cell_matrices,
                               std::vector<lbs::CellLBSView>& cell_transport_views,
                               std::vector<double>& destination_phi,
                               std::vector<double>& destination_psi,
                               const std::vector<double>& source_moments,
                               const LBSGroupset& groupset,
                               const std::map<int, std::shared_ptr<MultiGroupXS>>& xs,
                               int num_moments,
                               int max_num_cell_dofs)
  : SweepChunk(destination_phi,
               destination_psi,
               grid,
               discretization,
               unit_cell_matrices,
               cell_transport_views,
               source_moments,
               groupset,
               xs,
               num_moments,
               max_num_cell_dofs)
{}

void
AAH_SweepChunk::Sweep(AngleSet& angle_set)
{
  const SubSetInfo& grp_ss_info = groupset_.grp_subset_infos_[angle_set.GetRefGroupSubset()];

  auto gs_ss_size = grp_ss_info.ss_size;
  auto gs_ss_begin = grp_ss_info.ss_begin;
  auto gs_gi = groupset_.groups_[gs_ss_begin].id_;

  int deploc_face_counter = -1;
  int preloc_face_counter = -1;

  auto& fluds = dynamic_cast<AAH_FLUDS&>(angle_set.GetFLUDS());
  const auto& m2d_op = groupset_.quadrature_->GetMomentToDiscreteOperator();
  const auto& d2m_op = groupset_.quadrature_->GetDiscreteToMomentOperator();

  std::vector<std::vector<double>> Amat(max_num_cell_dofs_, std::vector<double>(max_num_cell_dofs_));
  std::vector<std::vector<double>> Atemp(max_num_cell_dofs_, std::vector<double>(max_num_cell_dofs_));
  std::vector<std::vector<double>> b(groupset_.groups_.size(), std::vector<double>(max_num_cell_dofs_));
  std::vector<double> source(max_num_cell_dofs_);

  // Loop over each cell
  const auto& spds = angle_set.GetSPDS();
  const auto& spls = spds.GetSPLS().item_id;
  const size_t num_spls = spls.size();
  for (size_t spls_index = 0; spls_index < num_spls; ++spls_index)
  {
    auto cell_local_id = spls[spls_index];
    auto& cell = grid_.local_cells[cell_local_id];
    auto& cell_mapping = grid_fe_view_.GetCellMapping(cell);
    auto& cell_transport_view = grid_transport_view_[cell_local_id];
    auto cell_num_faces = cell.faces_.size();
    auto cell_num_nodes = cell_mapping.NumNodes();

    const auto& face_orientations = spds.CellFaceOrientations()[cell_local_id];
    std::vector<double> face_mu_values(cell_num_faces);

    const auto& sigma_t = xs_.at(cell.material_id_)->SigmaTotal();
    
    // Get cell matrices
    const auto& G = unit_cell_matrices_[cell_local_id].intV_shapeI_gradshapeJ;
    const auto& M = unit_cell_matrices_[cell_local_id].intV_shapeI_shapeJ;
    const auto& M_surf = unit_cell_matrices_[cell_local_id].intS_shapeI_shapeJ;

    // Loop over angles in set (as = angleset, ss = subset)
    const int ni_deploc_face_counter = deploc_face_counter;
    const int ni_preloc_face_counter = preloc_face_counter;
    const std::vector<size_t>& as_angle_indices = angle_set.GetAngleIndices();
    for (size_t as_ss_idx = 0; as_ss_idx < as_angle_indices.size(); ++as_ss_idx)
    {
      auto direction_num = as_angle_indices[as_ss_idx];
      auto omega = groupset_.quadrature_->omegas_[direction_num];
      auto wt = groupset_.quadrature_->weights_[direction_num];

      deploc_face_counter = ni_deploc_face_counter;
      preloc_face_counter = ni_preloc_face_counter;

      // Reset right-hand side
      for (int gsg = 0; gsg < gs_ss_size; ++gsg)
        b[gsg].assign(cell_num_nodes, 0.0);

      for (int i = 0; i < cell_num_nodes; ++i)
        for (int j = 0; j < cell_num_nodes; ++j)
          Amat[i][j] = omega.Dot(G[i][j]);

      // Update face orientations
      for (int f = 0; f < cell_num_faces; ++f)
        face_mu_values[f] = omega.Dot(cell.faces_[f].normal_);

      // Surface integrals
      int in_face_counter = -1;
      for (int f = 0; f < cell_num_faces; ++f)
      {
        if (face_orientations[f] != FaceOrientation::INCOMING)
          continue;
   
        auto& cell_face = cell.faces_[f];
        const bool is_local_face = cell_transport_view.IsFaceLocal(f);
        const bool is_boundary_face = not cell_face.has_neighbor_;

        if (is_local_face)
          ++in_face_counter;
        else if (not is_boundary_face)
          ++preloc_face_counter;

        // IntSf_mu_psi_Mij_dA
        const size_t num_face_nodes = cell_mapping.NumFaceNodes(f);
        for (int fi = 0; fi < num_face_nodes; ++fi)
        {
          const int i = cell_mapping.MapFaceNode(f, fi);

          for (int fj = 0; fj < num_face_nodes; ++fj)
          {
            const int j = cell_mapping.MapFaceNode(f, fj);
          
            const double* psi;
            if (is_local_face)
              psi = fluds.UpwindPsi(spls_index, in_face_counter, fj, 0, as_ss_idx);
            else if (not is_boundary_face)
              psi = fluds.NLUpwindPsi(preloc_face_counter, fj, 0, as_ss_idx);
            else
              psi = angle_set.PsiBndry(cell_face.neighbor_id_,
                                       direction_num,
                                       cell_local_id,
                                       f,
                                       fj,
                                       gs_gi,
                                       gs_ss_begin,
                                       IsSurfaceSourceActive());
                                       
            const double mu_Nij = -face_mu_values[f] * M_surf[f][i][j];
            Amat[i][j] += mu_Nij;

            if (psi == nullptr)
              continue;

            for (int gsg = 0; gsg < gs_ss_size; ++gsg)
              b[gsg][i] += psi[gsg] * mu_Nij;
          } // for face node j
        }  // for face node i
      }  // for f

      // Looping over groups, assembling mass terms
      for (int gsg = 0; gsg < gs_ss_size; ++gsg)
      {
        double sigma_tg = sigma_t[gs_gi + gsg];

        // Contribute source moments q = M_n^T * q_moms
        for (int i = 0; i < cell_num_nodes; ++i)
        {
          double temp_src = 0.0;
          for (int m = 0; m < num_moments_; ++m)
          {
            const size_t ir = cell_transport_view.MapDOF(i, m, static_cast<int>(gs_gi + gsg));
            temp_src += m2d_op[m][direction_num] * q_moments_[ir];
          }
          source[i] = temp_src;
        }

        // Mass matrix and source
        // Atemp = Amat + sigma_tgr * M
        // b += M * q
        for (int i = 0; i < cell_num_nodes; ++i)
        {
          double temp = 0.0;
          for (int j = 0; j < cell_num_nodes; ++j)
          {
            const double Mij = M[i][j];
            Atemp[i][j] = Amat[i][j] + Mij * sigma_tg;
            temp += Mij * source[j];
          }
          b[gsg][i] += temp;
        }

        // Solve system
        GaussElimination(Atemp, b[gsg], static_cast<int>(cell_num_nodes));
      }

      // Flux updates      
      auto& output_phi = GetDestinationPhi();
      for (int m = 0; m < num_moments_; ++m)
      {
        const double wn_d2m = d2m_op[m][direction_num];
        for (int i = 0; i < cell_num_nodes; ++i)
        {
          const size_t ir = cell_transport_view.MapDOF(i, m, gs_gi);
          for (int gsg = 0; gsg < gs_ss_size; ++gsg)
            output_phi[ir + gsg] += wn_d2m * b[gsg][i];
        }
      }

      if (save_angular_flux_)
      {
        auto& output_psi = GetDestinationPsi();
        double* cell_psi_data =
          &output_psi[grid_fe_view_.MapDOFLocal(cell, 0, groupset_.psi_uk_man_, 0, 0)];

        for (size_t i = 0; i < cell_num_nodes; ++i)
        {
          const size_t imap = i * groupset_angle_group_stride_ +
                              direction_num * groupset_group_stride_ + gs_ss_begin;
          for (int gsg = 0; gsg < gs_ss_size; ++gsg)
            cell_psi_data[imap + gsg] = b[gsg][i];
        }
      }

      // Perform outgoing surface operations
      int out_face_counter = -1;
      for (int f = 0; f < cell_num_faces; ++f)
      {
        if (face_orientations[f] != FaceOrientation::OUTGOING)
          continue;

        // Set flags and counters
        out_face_counter++;
        const auto& face = cell.faces_[f];
        const bool is_local_face = cell_transport_view.IsFaceLocal(f);
        const bool is_boundary_face = not face.has_neighbor_;
       
        if (not is_boundary_face and not is_local_face)
          ++deploc_face_counter;

        bool is_reflecting_boundary_face =
          (is_boundary_face and angle_set.GetBoundaries()[face.neighbor_id_]->IsReflecting());

        const auto& IntF_shapeI = unit_cell_matrices_[cell_local_id].intS_shapeI[f];
      
        const size_t num_face_nodes = cell_mapping.NumFaceNodes(f);
        for (int fi = 0; fi < num_face_nodes; ++fi)
        {
          const int i = cell_mapping.MapFaceNode(f, fi);

          double* psi = nullptr;
          if (is_local_face)
            psi = fluds.OutgoingPsi(spls_index, out_face_counter, fi, as_ss_idx);
          else if (not is_boundary_face)
            psi = fluds.NLOutgoingPsi(deploc_face_counter, fi, as_ss_idx);
          else if (is_reflecting_boundary_face)
            psi = angle_set.ReflectingPsiOutBoundBndry(face.neighbor_id_,
                                                       direction_num,
                                                       cell_local_id,
                                                       f,
                                                       fi,
                                                       gs_ss_begin);

          if (psi)
          {
            if (not is_boundary_face or is_reflecting_boundary_face)
            {
              for (int gsg = 0; gsg < gs_ss_size; ++gsg)
                psi[gsg] = b[gsg][i];
            }
          }

          if (is_boundary_face and not is_reflecting_boundary_face)
          {
            for (int gsg = 0; gsg < gs_ss_size; ++gsg)
              cell_transport_view.AddOutflow(gs_gi + gsg, wt * face_mu_values[f] * b[gsg][i] * IntF_shapeI[i]);
          }
        } // for fi
      } // for face

    } // for n
  }   // for cell
}

} // namespace lbs
} // namespace opensn
