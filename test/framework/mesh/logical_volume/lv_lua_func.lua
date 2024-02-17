--[[# test for lus function used as a logical volume
--]]

-- set up orthogonal 3D geometry
nodes={}
N=50
L=5.0
xmin = -L/2
dx = L/N
for i=1,(N+1) do
  k=i-1
  nodes[i] = xmin + k*dx
end

meshgen = mesh.OrthogonalMeshGenerator.Create
({
  node_sets = {nodes,nodes,nodes},
})
mesh.MeshGenerator.Execute(meshgen)

-- assign mat ID 10 to whole domain
vol0 = mesh.RPPLogicalVolume.Create({infx=true, infy=true, infz=true})
mesh.SetProperty(MATID_FROMLOGICAL,vol0,10)

--Sets lua function describing a sphere (material 11)
function MatIDFunction1(x,y,z,cur_id)
    if (x*x + y*y + z*z < 1.0) then
        return 11
    end
    return cur_id
end
-- assign mat ID 11 to lv using lua function
mesh.SetProperty(MATID_FROM_LUA_FUNCTION, "MatIDFunction1")

-- export to vtk
mesh.ExportToVTK("lv_lua_func_out")

