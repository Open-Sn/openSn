// SPDX-FileCopyrightText: 2024 The OpenSn Authors <https://open-sn.github.io/opensn/>
// SPDX-License-Identifier: MIT

#include "modules/linear_boltzmann_solvers/response_evaluator/response_evaluator.h"
#include "modules/linear_boltzmann_solvers/lbs_solver/point_source/point_source.h"
#include "modules/linear_boltzmann_solvers/lbs_solver/volumetric_source/volumetric_source.h"
#include "modules/linear_boltzmann_solvers/lbs_solver/io/lbs_solver_io.h"
#include "framework/mesh/mesh_continuum/mesh_continuum.h"
#include "framework/logging/log.h"
#include "framework/object_factory.h"
#include "mpicpp-lite/mpicpp-lite.h"

namespace mpi = mpicpp_lite;

namespace opensn
{

OpenSnRegisterObjectInNamespace(lbs, ResponseEvaluator);

InputParameters
ResponseEvaluator::GetInputParameters()
{
  InputParameters params;
  params.SetGeneralDescription(
    "A utility class for evaluating responses using precomputed adjoint solutions "
    "and arbitrary forward sources.");
  params.SetDocGroup("LBSUtilities");

  params.AddRequiredParameter<size_t>("lbs_solver_handle", "A handle to an existing LBS solver.");
  params.AddOptionalParameterBlock(
    "options", ParameterBlock(), "The specification of adjoint buffers and forward to use.");
  params.LinkParameterToBlock("options", "response::OptionsBlock");

  return params;
}

ResponseEvaluator::ResponseEvaluator(const InputParameters& params)
  : lbs_solver_(
      GetStackItem<LBSSolver>(object_stack, params.GetParamValue<size_t>("lbs_solver_handle")))
{
  if (params.ParametersAtAssignment().Has("options"))
  {
    auto options = OptionsBlock();
    options.AssignParameters(params.GetParam("options"));
    SetOptions(options);
  }
}

OpenSnRegisterSyntaxBlockInNamespace(lbs, ResponseOptionsBlock, ResponseEvaluator::OptionsBlock);

InputParameters
ResponseEvaluator::OptionsBlock()
{
  InputParameters params;
  params.SetGeneralDescription("A block of options for the response evaluator for adding adjoint "
                               "buffers and defining forward sources.");
  params.SetDocGroup("LBSResponseEvaluator");

  params.AddOptionalParameterArray(
    "buffers", {}, "An array of tables containing adjoint buffer specifications.");
  params.LinkParameterToBlock("buffers", "response::BufferOptionsBlock");

  params.AddOptionalParameter("clear_sources", false, "A flag to clear existing sources.");
  params.AddOptionalParameterBlock(
    "sources", ParameterBlock(), "An array of tables containing source specification information.");
  params.LinkParameterToBlock("sources", "response::SourceOptionsBlock");

  return params;
}

void
ResponseEvaluator::SetOptions(const InputParameters& params)
{
  const auto& user_params = params.ParametersAtAssignment();

  if (user_params.Has("buffers"))
  {
    const auto& user_buffer_params = user_params.GetParam("buffers");
    user_buffer_params.RequireBlockTypeIs(ParameterBlockType::ARRAY);
    for (int p = 0; p < user_buffer_params.NumParameters(); ++p)
    {
      auto buffer_params = BufferOptionsBlock();
      buffer_params.AssignParameters(user_buffer_params.GetParam(p));
      SetBufferOptions(buffer_params);
    }
  }

  if (user_params.Has("clear_sources"))
    if (user_params.GetParamValue<bool>("clear_sources"))
    {
      material_sources_.clear();
      point_sources_.clear();
      volumetric_sources_.clear();
      boundary_sources_.clear();
    }

  if (user_params.Has("sources"))
  {
    auto source_params = SourceOptionsBlock();
    source_params.AssignParameters(user_params.GetParam("sources"));
    SetSourceOptions(source_params);
  }
}

OpenSnRegisterSyntaxBlockInNamespace(lbs,
                                     ResponseBufferOptionsBlock,
                                     ResponseEvaluator::BufferOptionsBlock);

InputParameters
ResponseEvaluator::BufferOptionsBlock()
{
  InputParameters params;
  params.SetGeneralDescription("Options for adding adjoint buffers to the response evaluator.");
  params.SetDocGroup("LBSResponseEvaluator");

  params.AddRequiredParameter<std::string>(
    "name",
    "A name given to the buffer to identify it when querying the response evaluation routine.");
  params.AddRequiredParameterBlock(
    "file_prefixes",
    "A table containing file prefixes for flux moments and angular flux binary files. "
    "These are keyed by \"flux_moments\" and \"angular_fluxes\", respectively.");

  return params;
}

void
ResponseEvaluator::SetBufferOptions(const InputParameters& params)
{
  const auto name = params.GetParamValue<std::string>("name");
  OpenSnInvalidArgumentIf(adjoint_buffers_.count(name) > 0,
                          "An adjoint buffer with name " + name + " already exists.");

  const auto prefixes = params.GetParam("file_prefixes");

  std::vector<double> phi;
  if (prefixes.Has("flux_moments"))
    LBSSolverIO::ReadFluxMoments(
      lbs_solver_, prefixes.GetParamValue<std::string>("flux_moments"), false, phi);

  std::vector<std::vector<double>> psi;
  if (prefixes.Has("angular_fluxes"))
    LBSSolverIO::ReadAngularFluxes(
      lbs_solver_, prefixes.GetParamValue<std::string>("angular_fluxes"), psi);

  adjoint_buffers_[name] = {phi, psi};
  log.Log0Verbose1() << "Adjoint buffer " << name << " added to the stack.";
}

OpenSnRegisterSyntaxBlockInNamespace(lbs,
                                     ResponseSourceOptionsBlock,
                                     ResponseEvaluator::SourceOptionsBlock);

InputParameters
ResponseEvaluator::SourceOptionsBlock()
{
  InputParameters params;
  params.SetGeneralDescription("A table of various forward source specifications.");
  params.SetDocGroup("LBSResponseEvaluator");

  params.AddOptionalParameterArray(
    "material", {}, "An array of tables containing material source specifications.");
  params.LinkParameterToBlock("material", "response::MaterialSourceOptionsBlock");

  params.AddOptionalParameterArray(
    "point", {}, "An array of tables containing point source handles.");

  params.AddOptionalParameterArray(
    "volumetric", {}, "An array of tables containing volumetric source handles.");

  params.AddOptionalParameterArray(
    "boundary", {}, "An array of tables containing boundary source specifications.");
  params.LinkParameterToBlock("boundary", "BoundaryOptionsBlock");

  return params;
}

void
ResponseEvaluator::SetSourceOptions(const InputParameters& params)
{
  params.RequireBlockTypeIs(ParameterBlockType::BLOCK);

  // Add material sources
  if (params.Has("material"))
  {
    const auto& user_msrc_params = params.GetParam("material");
    for (int p = 0; p < user_msrc_params.NumParameters(); ++p)
    {
      auto msrc_params = MaterialSourceOptionsBlock();
      msrc_params.AssignParameters(user_msrc_params.GetParam(p));
      SetMaterialSourceOptions(msrc_params);
    }
  }

  // Add point sources
  if (params.Has("point"))
  {
    const auto& user_psrc_params = params.GetParam("point");
    for (int p = 0; p < user_psrc_params.NumParameters(); ++p)
    {
      point_sources_.push_back(GetStackItem<PointSource>(
        object_stack, user_psrc_params.GetParam(p).GetValue<size_t>(), __FUNCTION__));
      point_sources_.back().Initialize(lbs_solver_);
    }
  }

  // Add volumetric sources
  if (params.Has("volumetric"))
  {
    const auto& user_dsrc_params = params.GetParam("volumetric");
    for (int p = 0; p < user_dsrc_params.NumParameters(); ++p)
    {
      volumetric_sources_.push_back(GetStackItem<VolumetricSource>(
        object_stack, user_dsrc_params.GetParam(p).GetValue<size_t>(), __FUNCTION__));
      volumetric_sources_.back().Initialize(lbs_solver_);
    }
  }

  // Add boundary sources
  if (params.Has("boundary"))
  {
    const auto& user_bsrc_params = params.GetParam("boundary");
    for (int p = 0; p < user_bsrc_params.NumParameters(); ++p)
    {
      auto bsrc_params = LBSSolver::BoundaryOptionsBlock();
      bsrc_params.AssignParameters(user_bsrc_params.GetParam(p));
      SetBoundarySourceOptions(bsrc_params);
    }
  }
}

OpenSnRegisterSyntaxBlockInNamespace(lbs,
                                     MaterialSourceOptionsBlock,
                                     ResponseEvaluator::MaterialSourceOptionsBlock);

InputParameters
ResponseEvaluator::MaterialSourceOptionsBlock()
{
  InputParameters params;
  params.SetGeneralDescription(
    "Options for adding material-based forward sources to the response evaluator.");
  params.SetDocGroup("LBSResponseEvaluator");

  params.AddRequiredParameter<int>("material_id", "The material id the source belongs to.");
  params.AddRequiredParameterArray("strength", "The group-wise material source strength.");

  return params;
}

void
ResponseEvaluator::SetMaterialSourceOptions(const InputParameters& params)
{
  const auto matid = params.GetParamValue<int>("material_id");
  OpenSnInvalidArgumentIf(material_sources_.count(matid) > 0,
                          "A material source for material id " + std::to_string(matid) +
                            " already exists.");

  const auto values = params.GetParamVectorValue<double>("strength");
  OpenSnInvalidArgumentIf(values.size() != lbs_solver_.NumGroups(),
                          "The number of material source values and groups "
                          "in the underlying solver do not match. "
                          "Expected " +
                            std::to_string(lbs_solver_.NumGroups()) + " but got " +
                            std::to_string(values.size()) + ".");

  material_sources_[matid] = values;
  log.Log0Verbose1() << "Material source for material id " << matid << " added to the stack.";
}

void
ResponseEvaluator::SetBoundarySourceOptions(const InputParameters& params)
{
  const auto bndry_name = params.GetParamValue<std::string>("name");
  const auto bndry_type = params.GetParamValue<std::string>("type");

  const auto bid = lbs_solver_.supported_boundary_names.at(bndry_name);
  if (bndry_type == "isotropic")
  {
    OpenSnInvalidArgumentIf(not params.Has("group_strength"),
                            "Parameter \"group_strength\" is required for "
                            "boundaries of type \"isotropic\".");
    params.RequireParameterBlockTypeIs("values", ParameterBlockType::ARRAY);

    boundary_sources_[bid] = {BoundaryType::ISOTROPIC,
                              params.GetParamVectorValue<double>("group_strength")};
  }
  else
    log.Log0Warning() << "Unsupported boundary type. Skipping the entry.";
}

void
ResponseEvaluator::ClearForwardSources()
{
  material_sources_.clear();
  point_sources_.clear();
  volumetric_sources_.clear();
  boundary_sources_.clear();
}

double
ResponseEvaluator::EvaluateResponse(const std::string& buffer) const
{
  const auto& buffer_data = adjoint_buffers_.at(buffer);
  const auto& phi_dagger = buffer_data.first;
  const auto& psi_dagger = buffer_data.second;

  OpenSnLogicalErrorIf(not material_sources_.empty() and phi_dagger.empty(),
                       "If material sources are present, adjoint flux moments "
                       "must be available for response evaluation.");
  OpenSnLogicalErrorIf(not point_sources_.empty() and phi_dagger.empty(),
                       "If point sources are set, adjoint flux moments "
                       "must be available for response evaluation.");
  OpenSnLogicalErrorIf(not volumetric_sources_.empty() and phi_dagger.empty(),
                       "if volumetric sources are set, adjoint flux moments "
                       "must be available for response evaluation.");
  OpenSnLogicalErrorIf(not boundary_sources_.empty() and psi_dagger.empty(),
                       "If boundary sources are set, adjoint angular fluxes "
                       "must be available for response evaluation.");

  const auto& grid = lbs_solver_.Grid();
  const auto& discretization = lbs_solver_.SpatialDiscretization();
  const auto& transport_views = lbs_solver_.GetCellTransportViews();
  const auto& unit_cell_matrices = lbs_solver_.GetUnitCellMatrices();
  const auto num_groups = lbs_solver_.NumGroups();

  double local_response = 0.0;

  // Material sources
  if (not material_sources_.empty())
  {
    for (const auto& cell : grid.local_cells)
    {
      const auto& cell_mapping = discretization.GetCellMapping(cell);
      const auto& transport_view = transport_views[cell.local_id];
      const auto& fe_values = unit_cell_matrices[cell.local_id];
      const auto num_cell_nodes = cell_mapping.NumNodes();

      if (material_sources_.count(cell.material_id) > 0)
      {
        const auto& src = material_sources_.at(cell.material_id);
        for (size_t i = 0; i < num_cell_nodes; ++i)
        {
          const auto dof_map = transport_view.MapDOF(i, 0, 0);
          const auto& V_i = fe_values.intV_shapeI(i);
          for (size_t g = 0; g < num_groups; ++g)
            local_response += src[g] * phi_dagger[dof_map + g] * V_i;
        }
      }
    } // for cell
  }   // if material sources

  // Boundary sources
  if (not boundary_sources_.empty())
  {
    size_t gs = 0;
    for (const auto& groupset : lbs_solver_.Groupsets())
    {
      const auto& uk_man = groupset.psi_uk_man_;
      const auto& quadrature = groupset.quadrature;
      const auto& num_gs_angles = quadrature->omegas.size();
      const auto& num_gs_groups = groupset.groups.size();

      for (const auto& cell : grid.local_cells)
      {
        const auto& cell_mapping = discretization.GetCellMapping(cell);
        const auto& fe_values = unit_cell_matrices[cell.local_id];

        size_t f = 0;
        for (const auto& face : cell.faces)
        {
          if (not face.has_neighbor and boundary_sources_.count(face.neighbor_id) > 0)
          {
            const auto bndry_id = face.neighbor_id;
            const auto num_face_nodes = cell_mapping.NumFaceNodes(f);
            for (size_t fi = 0; fi < num_face_nodes; ++fi)
            {
              const auto i = cell_mapping.MapFaceNode(f, fi);
              const auto& node = grid.vertices[cell.vertex_ids[i]];
              const auto& intF_shapeI = fe_values.intS_shapeI[f](i);

              const auto psi_bndry = EvaluateBoundaryCondition(bndry_id, node, groupset);

              for (size_t n = 0; n < num_gs_angles; ++n)
              {
                const auto& omega = quadrature->omegas[n];
                const auto mu = omega.Dot(face.normal);
                if (mu < 0.0)
                {
                  const auto& wt = quadrature->weights[n];
                  const auto weight = -mu * wt * intF_shapeI;
                  const auto dof_map = discretization.MapDOFLocal(cell, i, uk_man, n, 0);

                  for (size_t gsg = 0; gsg < num_gs_groups; ++gsg)
                    local_response +=
                      weight * psi_dagger[gs][dof_map + gsg] * psi_bndry[num_gs_groups * n + gsg];
                } // if outgoing
              }
            } // for face node fi
          }
          ++f;
        } // for face
      }   // for cell
      ++gs;
    } // for groupset
  }   // if boundary sources

  // Point sources
  for (const auto& point_source : point_sources_)
    for (const auto& subscriber : point_source.Subscribers())
    {
      const auto& cell = grid.local_cells[subscriber.cell_local_id];
      const auto& transport_view = transport_views[cell.local_id];

      const auto& src = point_source.Strength();
      const auto& vol_wt = subscriber.volume_weight;

      const auto num_cell_nodes = transport_view.NumNodes();
      for (size_t i = 0; i < num_cell_nodes; ++i)
      {
        const auto dof_map = transport_view.MapDOF(i, 0, 0);
        const auto& shape_val = subscriber.shape_values(i);
        for (size_t g = 0; g < num_groups; ++g)
          local_response += vol_wt * shape_val * src[g] * phi_dagger[dof_map + g];
      } // for node i
    }   // for subscriber

  // Volumetric sources
  for (const auto& volumetric_source : volumetric_sources_)
    for (const uint64_t local_id : volumetric_source.GetSubscribers())
    {
      const auto& cell = grid.local_cells[local_id];
      const auto& transport_view = transport_views[cell.local_id];
      const auto& fe_values = unit_cell_matrices[cell.local_id];
      const auto& nodes = discretization.CellNodeLocations(cell);

      const auto num_cell_nodes = transport_view.NumNodes();
      for (size_t i = 0; i < num_cell_nodes; ++i)
      {
        const auto& V_i = fe_values.intV_shapeI(i);
        const auto dof_map = transport_view.MapDOF(i, 0, 0);
        const auto& vals = volumetric_source(cell, nodes[i], num_groups);
        for (size_t g = 0; g < num_groups; ++g)
          local_response += vals[g] * phi_dagger[dof_map + g] * V_i;
      }
    }

  double global_response = 0.0;
  mpi_comm.all_reduce(local_response, global_response, mpi::op::sum<double>());
  return global_response;
}

std::vector<double>
ResponseEvaluator::EvaluateBoundaryCondition(const uint64_t boundary_id,
                                             const Vector3& node,
                                             const LBSGroupset& groupset,
                                             const double) const
{
  const auto num_gs_angles = groupset.quadrature->omegas.size();
  const auto num_gs_groups = groupset.groups.size();
  const auto first_group = groupset.groups.front().id;

  std::vector<double> psi;
  const auto& bc = boundary_sources_.at(boundary_id);
  if (bc.type == BoundaryType::ISOTROPIC)
  {
    for (size_t n = 0; n < num_gs_angles; ++n)
      for (size_t gsg = 0; gsg < num_gs_groups; ++gsg)
        psi.emplace_back(bc.isotropic_mg_source[first_group + gsg]);
    return psi;
  }
  OpenSnLogicalError("Unexpected behavior. Unsupported boundary condition encountered.");
}

} // namespace opensn
