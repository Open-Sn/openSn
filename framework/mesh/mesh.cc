// SPDX-FileCopyrightText: 2024 The OpenSn Authors <https://open-sn.github.io/opensn/>
// SPDX-License-Identifier: MIT

#include "framework/mesh/mesh.h"
#include "framework/runtime.h"
#include "framework/mesh/unpartitioned_mesh/unpartitioned_mesh.h"
#include "framework/logging/log.h"

namespace opensn
{

std::shared_ptr<MeshContinuum>
CurrentMesh()
{
  if (mesh_stack.empty())
    throw std::logic_error(
      "Empty mesh stack. Ensure that \"mesh.MeshGenerator.Execute\" is called in the input.");
  return mesh_stack.back();
}

} // namespace opensn
