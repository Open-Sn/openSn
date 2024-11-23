-- 2D test to compute and display volume per material ID.
num_procs = 3

-- ############################################### Check num_procs
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

-- ############################################### Setup mesh
nodes = {}
N = 20
L = 5
xmin = -L / 2
dx = L / N
for i = 1, (N + 1) do
  k = i - 1
  nodes[i] = xmin + k * dx
end

meshgen1 = mesh.OrthogonalMeshGenerator.Create({ node_sets = { nodes, nodes } })
mesh.MeshGenerator.Execute(meshgen1)

-- ############################################### Set Material IDs
vol0 = logvol.RPPLogicalVolume.Create({ infx = true, infy = true, infz = true })
mesh.SetMaterialIDFromLogicalVolume(vol0, 0)
vol1 = logvol.RPPLogicalVolume.Create({ xmin = -1000.0, xmax = 0.0, infy = true, infz = true })
mesh.SetMaterialIDFromLogicalVolume(vol1, 1)

mesh.ComputeVolumePerMaterialID()
