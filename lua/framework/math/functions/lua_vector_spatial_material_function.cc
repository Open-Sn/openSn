// SPDX-FileCopyrightText: 2024 The OpenSn Authors <https://open-sn.github.io/opensn/>
// SPDX-License-Identifier: MIT

#include "lua/framework/math/functions/lua_vector_spatial_material_function.h"
#include "lua/framework/lua.h"
#include "lua/framework/console/console.h"
#include "framework/runtime.h"
#include "framework/object_factory.h"

using namespace opensn;

namespace opensnlua
{

OpenSnRegisterObjectInNamespace(opensn, LuaVectorSpatialMaterialFunction);

InputParameters
LuaVectorSpatialMaterialFunction::GetInputParameters()
{
  InputParameters params = VectorSpatialMaterialFunction::GetInputParameters();
  params.AddRequiredParameter<std::string>("lua_function_name", "Name of the lua function");
  return params;
}

LuaVectorSpatialMaterialFunction::LuaVectorSpatialMaterialFunction(const InputParameters& params)
  : opensn::VectorSpatialMaterialFunction(params),
    lua_function_name_(params.ParamValue<std::string>("lua_function_name"))
{
}

std::vector<double>
LuaVectorSpatialMaterialFunction::Evaluate(const opensn::Vector3& xyz,
                                           int mat_id,
                                           int num_components) const
{
  // Load lua function
  lua_State* L = console.GetConsoleState();
  auto lua_return = LuaCall<std::vector<double>>(L, lua_function_name_, xyz, mat_id);

  // Check return value
  OpenSnLogicalErrorIf(lua_return.size() != num_components,
                       "Call to lua function " + lua_function_name_ +
                         " returned a vector of size " + std::to_string(lua_return.size()) +
                         ", which is not the same as the number of groups " +
                         std::to_string(num_components) + ".");

  return lua_return;
}

} // namespace opensnlua
