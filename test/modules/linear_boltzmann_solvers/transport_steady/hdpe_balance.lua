-- Infinite 172-group problem
-- Create Mesh
nodes = {}
N = 2
L = 10
xmin = -L / 2
dx = L / N
for i = 1, (N + 1) do
  k = i - 1
  nodes[i] = xmin + k * dx
end

meshgen = mesh.OrthogonalMeshGenerator.Create({ node_sets = { nodes, nodes, nodes } })
mesh.MeshGenerator.Execute(meshgen)

-- Set Material IDs
mesh.SetUniformMaterialID(0)

materials = {}
materials[1] = mat.AddMaterial("HDPE")

num_groups = 172

-- Add cross sections to materials
mat.SetProperty(materials[1], TRANSPORT_XSECTIONS, OPENMC_XSLIB, "HDPE.h5", 294.0)

src = {}
for g = 1, num_groups do
  src[g] = 0.0
end
src[1] = 1.0
mat.SetProperty(materials[1], ISOTROPIC_MG_SOURCE, FROM_ARRAY, src)

-- Angular Quadrature
pquad = aquad.CreateProductQuadrature(GAUSS_LEGENDRE_CHEBYSHEV, 2, 2)

-- LBS block option
lbs_block = {
  num_groups = num_groups,
  groupsets = {
    {
      groups_from_to = { 0, num_groups - 1 },
      angular_quadrature_handle = pquad,
      inner_linear_method = "gmres",
      l_abs_tol = 1.0e-9,
      l_max_its = 300,
      gmres_restart_interval = 30,
    },
  },
  options = {
    scattering_order = 0,
    spatial_discretization = "pwld",
    save_angular_flux = true,
    boundary_conditions = {
      { name = "xmin", type = "reflecting" },
      { name = "xmax", type = "reflecting" },
      { name = "ymin", type = "reflecting" },
      { name = "ymax", type = "reflecting" },
      { name = "zmin", type = "reflecting" },
      { name = "zmax", type = "reflecting" },
    },
  },
}

phys = lbs.DiscreteOrdinatesSolver.Create(lbs_block)

-- Initialize and execute solver
ss_solver = lbs.SteadyStateSolver.Create({ lbs_solver_handle = phys })

solver.Initialize(ss_solver)
solver.Execute(ss_solver)

-- compute particle balance
lbs.ComputeBalance(phys)
