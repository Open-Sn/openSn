-- 3D transport restart test with vacuum and incident-isotropic boundary condtions
-- SDM: PWLD
-- Test: Max-value=5.88996

-- Set and check number of processors
num_procs = 4
if check_num_procs == nil and number_of_processes ~= num_procs then
  log.Log(
    LOG_0ERROR,
    "Incorrect amount of processors. "
      .. "Expected "
      .. tostring(num_procs)
      .. ". Pass check_num_procs=false to override if possible."
  )
  os.exit(false)
end

-- Setup mesh
meshgen1 = mesh.ExtruderMeshGenerator.Create({
  inputs = {
    mesh.FromFileMeshGenerator.Create({
      filename = "../../../assets/mesh/TriangleMesh2x2Cuts.obj",
    }),
  },
  layers = { { z = 0.4, n = 2 }, { z = 0.8, n = 2 }, { z = 1.2, n = 2 }, { z = 1.6, n = 2 } }, -- layers
  partitioner = mesh.KBAGraphPartitioner.Create({
    nx = 2,
    ny = 2,
    xcuts = { 0.0 },
    ycuts = { 0.0 },
  }),
})
mesh.MeshGenerator.Execute(meshgen1)

-- Set material IDs
vol0 = logvol.RPPLogicalVolume.Create({ infx = true, infy = true, infz = true })
mesh.SetMaterialIDFromLogicalVolume(vol0, 0)

vol1 =
  logvol.RPPLogicalVolume.Create({ xmin = -0.5, xmax = 0.5, ymin = -0.5, ymax = 0.5, infz = true })
mesh.SetMaterialIDFromLogicalVolume(vol1, 1)

-- Add materials
materials = {}
materials[1] = mat.AddMaterial("Test Material")
materials[2] = mat.AddMaterial("Test Material2")

num_groups = 1
mat.SetProperty(materials[1], TRANSPORT_XSECTIONS, SIMPLE_ONE_GROUP, 1000.0, 0.9999)
mat.SetProperty(materials[2], TRANSPORT_XSECTIONS, SIMPLE_ONE_GROUP, 1000.0, 0.9999)

src = {}
for g = 1, num_groups do
  src[g] = 0.5
end

mat.SetProperty(materials[1], ISOTROPIC_MG_SOURCE, FROM_ARRAY, src)
mat.SetProperty(materials[2], ISOTROPIC_MG_SOURCE, FROM_ARRAY, src)

--Setup physics
pquad0 = aquad.CreateProductQuadrature(GAUSS_LEGENDRE_CHEBYSHEV, 2, 2)

lbs_block = {
  num_groups = num_groups,
  groupsets = {
    {
      groups_from_to = { 0, 0 },
      angular_quadrature_handle = pquad0,
      angle_aggregation_num_subsets = 1,
      groupset_num_subsets = 1,
      inner_linear_method = "petsc_gmres",
      l_abs_tol = 1.0e-6,
      l_max_its = 10000,
      gmres_restart_interval = 100,
    },
  },
}
bsrc = {}
for g = 1, num_groups do
  bsrc[g] = 0.0
end
bsrc[1] = 1.0 / 4.0 / math.pi
lbs_options = {
  boundary_conditions = {
    { name = "zmax", type = "isotropic", group_strength = bsrc },
  },
  scattering_order = 1,
  save_angular_flux = true,
  --restart_writes_enabled = true,
  --write_delayed_psi_to_restart = true,
  --write_restart_path = "transport_3d_2_unstructured_restart/transport_3d_2_unstructured",
  read_restart_path = "transport_3d_2_unstructured_restart/transport_3d_2_unstructured",
}

phys1 = lbs.DiscreteOrdinatesSolver.Create(lbs_block)
lbs.SetOptions(phys1, lbs_options)

--Initialize and execute solver
ss_solver = lbs.SteadyStateSolver.Create({ lbs_solver_handle = phys1 })

solver.Initialize(ss_solver)
solver.Execute(ss_solver)

--Get field functions
fflist, count = lbs.GetScalarFieldFunctionList(phys1)

-- Volume integrations
ffi1 = fieldfunc.FFInterpolationCreate(VOLUME)
curffi = ffi1
fieldfunc.SetProperty(curffi, OPERATION, OP_MAX)
fieldfunc.SetProperty(curffi, LOGICAL_VOLUME, vol0)
fieldfunc.SetProperty(curffi, ADD_FIELDFUNCTION, fflist[1])

fieldfunc.Initialize(curffi)
fieldfunc.Execute(curffi)
maxval = fieldfunc.GetValue(curffi)

log.Log(LOG_0, string.format("Max-value1=%.5e", maxval))

ffi1 = fieldfunc.FFInterpolationCreate(VOLUME)
