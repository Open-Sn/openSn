// SPDX-FileCopyrightText: 2024 The OpenSn Authors <https://open-sn.github.io/opensn/>
// SPDX-License-Identifier: MIT

#include "framework/field_functions/field_function_interface.h"
#include "framework/field_functions/field_function.h"
#include "framework/runtime.h"
#include "framework/logging/log.h"

namespace opensn
{

InputParameters
FieldFunctionInterface::GetInputParameters()
{
  InputParameters params;

  params.AddRequiredParameterBlock("field_function", "Field function handle or name.");
  params.SetParameterTypeMismatchAllowed("field_function");

  return params;
}

FieldFunctionInterface::FieldFunctionInterface(const InputParameters& params)
  : field_function_param_(params.Param("field_function"))
{
}

FieldFunction*
FieldFunctionInterface::GetFieldFunction() const
{
  std::shared_ptr<FieldFunction> ref_ff_ptr = nullptr;
  if (field_function_param_.Type() == ParameterBlockType::STRING)
  {
    const auto name = field_function_param_.Value<std::string>();
    for (const auto& ff_ptr : field_function_stack)
      if (ff_ptr->Name() == name)
        ref_ff_ptr = ff_ptr;

    OpenSnInvalidArgumentIf(ref_ff_ptr == nullptr, "Field function \"" + name + "\" not found.");
  }
  else if (field_function_param_.Type() == ParameterBlockType::INTEGER)
  {
    const auto handle = field_function_param_.Value<size_t>();
    ref_ff_ptr = GetStackItemPtrAsType<FieldFunction>(field_function_stack, handle, __FUNCTION__);
  }
  else
    OpenSnInvalidArgument("Argument can only be STRING or INTEGER");

  return &(*ref_ff_ptr);
}

} // namespace opensn
