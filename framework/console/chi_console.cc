#include "console/chi_console.h"
#include "lua/chi_modules_lua.h"
#include "chi_configuration.h"
#include "ChiObjectFactory.h"
#include "chi_runtime.h"
#include "chi_log.h"
#include "chi_log_exceptions.h"
#include "chi_lua.h"
#include "chi_mpi.h"
#include "chi_console_structs.h"
#include "chi_utils.h"
#if defined(__MACH__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif
#include <iostream>

namespace chi::lua_utils
{
int chiMakeObject(lua_State* L);
}

namespace chi
{

RegisterLuaFunction(Console::LuaWrapperCall, chi_console, LuaWrapperCall);

Console&
Console::GetInstance() noexcept
{
  static Console singleton;
  return singleton;
}

Console::Console() noexcept : console_state_(luaL_newstate())
{
}

void
Console::LoadRegisteredLuaItems()
{
  //=================================== Initializing console
  auto& L = GetConsoleState();

  luaL_openlibs(L);

  //=================================== Register version
  lua_pushstring(L, PROJECT_VERSION);
  lua_setglobal(L, "chi_version");
  lua_pushinteger(L, PROJECT_MAJOR_VERSION);
  lua_setglobal(L, "chi_major_version");
  lua_pushinteger(L, PROJECT_MINOR_VERSION);
  lua_setglobal(L, "chi_minor_version");
  lua_pushinteger(L, PROJECT_PATCH_VERSION);
  lua_setglobal(L, "chi_patch_version");

  //=================================== Registering functions
  chi_modules::lua_utils::RegisterLuaEntities(L);

  //=================================== Registering static-registration
  //                                    lua functions
  for (const auto& [key, entry] : lua_function_registry_)
    SetLuaFuncNamespaceTableStructure(key, entry.function_ptr);

  //=================================== Registering LuaFunctionWrappers
  for (const auto& [key, entry] : function_wrapper_registry_)
    if (entry.call_func) SetLuaFuncWrapperNamespaceTableStructure(key);

  for (const auto& [key, value] : lua_constants_registry_)
    SetLuaConstant(key, value);

  //=================================== Registering solver-function
  //                                    scope resolution tables
  const auto& object_maker = ChiObjectFactory::GetInstance();
  for (const auto& entry : object_maker.Registry())
    SetObjectNamespaceTableStructure(entry.first);
}

void
Console::FlushConsole()
{
  try
  {
    for (auto& command : command_buffer_)
    {
      bool error = luaL_dostring(console_state_, command.c_str());
      if (error)
      {
        Chi::log.LogAll() << lua_tostring(console_state_, -1);
        lua_pop(console_state_, 1);
      }
    }
  }
  catch (const std::exception& e)
  {
    Chi::log.LogAllError() << e.what();
    Chi::Exit(EXIT_FAILURE);
  }
}

int
Console::LuaWrapperCall(lua_State* L)
{
  const int num_args = lua_gettop(L);
  // We do not check for the required parameters here because we want
  // to make this function call as fast as possible. Besides, via the
  // static registration we should never run into an issue here.

  auto& console = Console::GetInstance();

  const auto& registry = console.function_wrapper_registry_;

  const std::string fname = lua_tostring(L, 1);

  ChiLogicalErrorIf(registry.count(fname) == 0,
                    std::string("Wrapper with name \"") + fname + "\" not in console registry.");

  const auto& reg_entry = registry.at(fname);

  auto input_params = reg_entry.get_in_params_func();

  ParameterBlock main_arguments_block;
  for (int p = 2; p <= num_args; ++p)
  {
    const std::string arg_name = "arg" + std::to_string(p - 2);

    if (lua_isboolean(L, p)) main_arguments_block.AddParameter(arg_name, lua_toboolean(L, p));
    else if (lua_isinteger(L, p))
      main_arguments_block.AddParameter(arg_name, lua_tointeger(L, p));
    else if (lua_isnumber(L, p))
      main_arguments_block.AddParameter(arg_name, lua_tonumber(L, p));
    else if (lua_isstring(L, p))
      main_arguments_block.AddParameter(arg_name, lua_tostring(L, p));
    else if (lua_istable(L, p))
    {
      auto block = chi_lua::TableParserAsParameterBlock::ParseTable(L, p);
      block.SetBlockName(arg_name);
      std::string scope = fname + ":";
      scope.append(arg_name + " ");
      block.SetErrorOriginScope(scope);
      main_arguments_block.AddParameter(block);
    }
    else
      ChiInvalidArgument("In call to \"" + fname +
                         "\": Unsupported argument "
                         "type \"" +
                         lua_typename(L, lua_type(L, p)) + "\" encountered.");
  }
  // Set input parameters here
  input_params.SetErrorOriginScope(fname + "()");
  input_params.AssignParameters(main_arguments_block);

  auto output_params = reg_entry.call_func(input_params);

  output_params.SetErrorOriginScope(fname + ":output:");
  chi_lua::PushParameterBlock(L, output_params);

  const int num_sub_params = static_cast<int>(output_params.NumParameters());

  return output_params.IsScalar() ? 1 : num_sub_params;
}

void
Console::RunConsoleLoop(char*) const
{
  Chi::log.Log() << "Console loop started. "
                 << "Type \"exit\" to quit (or Ctl-C).";

  /** Wrapper to an MPI_Bcast call for a single integer
   * broadcast from location 0. */
  auto BroadcastSingleInteger = [](int* int_being_bcast)
  {
    MPI_Bcast(int_being_bcast, // buffer
              1,
              MPI_INT,        // count + type
              0,              // root
              Chi::mpi.comm); // communicator
  };

  /** Wrapper to an MPI_Bcast call for an array of characters
   * broadcast from location 0. */
  auto HomeBroadcastStringAsRaw = [](std::string string_to_bcast, int length)
  {
    char* raw_string_to_bcast = string_to_bcast.data();
    MPI_Bcast(raw_string_to_bcast, // buffer
              length,
              MPI_CHAR,       // count + type
              0,              // root
              Chi::mpi.comm); // communicator
  };

  /** Wrapper to an MPI_Bcast call for an array of characters
   * broadcast from location 0. This call is for non-home locations. */
  auto NonHomeBroadcastStringAsRaw = [](std::string& string_to_bcast, int length)
  {
    std::vector<char> raw_chars(length + 1, '\0');
    MPI_Bcast(raw_chars.data(), // buffer
              length,
              MPI_CHAR,       // count + type
              0,              // root
              Chi::mpi.comm); // communicator

    string_to_bcast = std::string(raw_chars.data());
  };

  /** Executes a string within the lua-console. */
  auto LuaDoString = [this](const std::string& the_string)
  {
    bool error = luaL_dostring(console_state_, the_string.c_str());
    if (error)
    {
      Chi::log.LogAll() << lua_tostring(console_state_, -1);
      lua_pop(console_state_, 1);
    }
  };

  auto ConsoleInputNumChars = [](const std::string& input)
  {
    int L = static_cast<int>(input.size());
    if (input == std::string("exit")) L = -1;

    return L;
  };

  const bool HOME = Chi::mpi.location_id == 0;

  while (not Chi::run_time::termination_posted_)
  {
    std::string console_input;

    if (HOME) std::cin >> console_input; // Home will be waiting here

    int console_input_len = ConsoleInputNumChars(console_input);

    BroadcastSingleInteger(&console_input_len); // Non-Home locs wait here

    if (console_input_len < 0) break;
    else if (HOME)
      HomeBroadcastStringAsRaw(console_input, console_input_len);
    else
      NonHomeBroadcastStringAsRaw(console_input, console_input_len);

    try
    {
      LuaDoString(console_input);
    }
    catch (const Chi::RecoverableException& e)
    {
      Chi::log.LogAllError() << e.what();
    }
    catch (const std::exception& e)
    {
      Chi::log.LogAllError() << e.what();
      Chi::Exit(EXIT_FAILURE);
    }
  } // while not termination posted

  Chi::run_time::termination_posted_ = true;

  Chi::log.Log() << "Console loop stopped successfully.";
}

CSTMemory
Console::GetMemoryUsage()
{
  double mem = 0.0;
#if defined(__MACH__)
  struct mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  long long int bytes;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
  {
    bytes = 0;
  }
  bytes = info.resident_size;
  mem = (double)bytes;
#else
  long long int llmem = 0;
  long long int rss = 0;

  std::string ignore;
  std::ifstream ifs("/proc/self/stat", std::ios_base::in);
  ifs >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >>
    ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >>
    ignore >> ignore >> ignore >> ignore >> llmem >> rss;

  long long int page_size_bytes = sysconf(_SC_PAGE_SIZE);
  mem = rss * page_size_bytes;
  /*
  FILE* fp = NULL;
  if((fp = fopen( "/proc/self/statm", "r" )) == NULL)
    return 0;
  if(fscanf(fp, "%*s%*s%*s%*s%*s%lld", &llmem) != 1)
  {
    fclose(fp);
    return 0;
  }
  fclose(fp);*/

  // mem = llmem * (long long int)sysconf(_SC_PAGESIZE);
#endif

  CSTMemory mem_struct(mem);

  return mem_struct;
}

double
Console::GetMemoryUsageInMB()
{
  CSTMemory mem_struct = GetMemoryUsage();

  return mem_struct.memory_mbytes;
}

// Execute file
/** Executes the given file in the Lua engine.
\author Jan*/
int
chi::Console::ExecuteFile(const std::string& fileName, int argc, char** argv) const
{
  lua_State* L = this->console_state_;
  if (not fileName.empty())
  {
    if (argc > 0)
    {
      lua_newtable(L);
      for (int i = 1; i <= argc; i++)
      {
        lua_pushnumber(L, i);
        lua_pushstring(L, argv[i - 1]);
        lua_settable(L, -3);
      }
      lua_setglobal(L, "chiArgs");
    }
    int error = luaL_dofile(this->console_state_, fileName.c_str());

    if (error > 0)
    {
      Chi::log.LogAllError() << "LuaError: " << lua_tostring(this->console_state_, -1);
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}

// ###################################################################
/**Pushes location id and number of processes to lua state.*/
void
chi::Console::PostMPIInfo(int location_id, int number_of_processes) const
{
  lua_State* L = this->console_state_;

  lua_pushinteger(L, location_id);
  lua_setglobal(L, "chi_location_id");

  lua_pushinteger(L, number_of_processes);
  lua_setglobal(L, "chi_number_of_processes");
}

// ###################################################################
/**Basic addition to registry. Used by the other public methods
 * to registry a text-key to a lua function.*/
void
chi::Console::AddFunctionToRegistry(const std::string& name_in_lua, lua_CFunction function_ptr)
{
  auto& console = GetInstance();

  // Check if the function name is already there
  if (console.lua_function_registry_.count(name_in_lua) > 0)
  {
    const auto& current_entry = console.lua_function_registry_.at(name_in_lua);

    throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                           ": Attempted "
                           "to register lua function \"" +
                           name_in_lua +
                           "\" but the function "
                           "is already taken by " +
                           current_entry.function_raw_name);
  }

  console.lua_function_registry_.insert(
    std::make_pair(name_in_lua, LuaFunctionRegistryEntry{function_ptr, name_in_lua}));
}

// ###################################################################
/**Adds a lua_CFunction to the registry. The registry of functions gets
 * parsed into the lua console when `chi::Initialize` is called. This
 * particular function will strip the namespace from the the parameter
 * `raw_name_in_lua` and cause the function to be registered in the
 * global namespace of the lua console.*/
char
chi::Console::AddFunctionToRegistryGlobalNamespace(const std::string& raw_name_in_lua,
                                                   lua_CFunction function_ptr)
{
  // Filter out namespace from the raw name
  const std::string name_in_lua = chi::StringUpToFirstReverse(raw_name_in_lua, "::");

  AddFunctionToRegistry(name_in_lua, function_ptr);

  return 0;
}

// ###################################################################
/**Adds a lua_CFunction to the registry. The registry of functions gets
 * parsed into the lua console when `chi::Initialize` is called. The full
 * path of the function will be derived from `namespace_name` + "::" +
 * `function_name`.*/
char
chi::Console::AddFunctionToRegistryInNamespaceWithName(lua_CFunction function_ptr,
                                                       const std::string& namespace_name,
                                                       const std::string& function_name)
{
  const std::string name_in_lua = namespace_name + "::" + function_name;

  AddFunctionToRegistry(name_in_lua, function_ptr);

  return 0;
}

// ###################################################################
/**\brief Adds a constant to the lua state. Prepending the constant
 * within a namespace is optional.*/
char
chi::Console::AddLuaConstantToRegistry(const std::string& namespace_name,
                                       const std::string& constant_name,
                                       const chi_data_types::Varying& value)
{
  const std::string name_in_lua = namespace_name + "::" + constant_name;

  // Check if the constant name is already there
  auto& console = Console::GetInstance();
  if (console.lua_constants_registry_.count(name_in_lua) > 0)
  {
    throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                           ": Attempted "
                           "to register lua const  \"" +
                           name_in_lua +
                           "\" but the value "
                           "is already taken.");
  }

  console.lua_constants_registry_.insert(std::make_pair(name_in_lua, value));
  return 0;
}

// ###################################################################
chi::InputParameters
chi::Console::DefaultGetInParamsFunc()
{
  return InputParameters();
}

// ###################################################################
/**Wrapper functions operate with input and output parameters, essentially
 * hiding the lua interface.*/
char
chi::Console::AddWrapperToRegistryInNamespaceWithName(const std::string& namespace_name,
                                                      const std::string& name_in_lua,
                                                      WrapperGetInParamsFunc syntax_function,
                                                      WrapperCallFunc actual_function,
                                                      bool ignore_null_call_func /*=false*/)
{
  const std::string name =
    (namespace_name.empty()) ? name_in_lua : namespace_name + "::" + name_in_lua;

  auto& console = GetInstance();
  auto& registry = console.function_wrapper_registry_;

  ChiLogicalErrorIf(registry.count(name) > 0,
                    std::string("Attempted to register lua-function wrapper \"") + name +
                      "\" but a wrapper with the same name already exists");

  if (not syntax_function) syntax_function = DefaultGetInParamsFunc;

  if (not ignore_null_call_func)
    ChiLogicalErrorIf(not actual_function, "Problem with get_in_params_func");

  LuaFuncWrapperRegEntry reg_entry;
  reg_entry.get_in_params_func = syntax_function;
  reg_entry.call_func = actual_function;

  registry.insert(std::make_pair(name, reg_entry));

  return 0;
}

// ###################################################################
/**Sets/Forms a lua function in the state using a namespace structure.*/
void
chi::Console::SetLuaFuncNamespaceTableStructure(const std::string& full_lua_name,
                                                lua_CFunction function_ptr)
{
  auto L = GetInstance().console_state_;
  const auto lua_name_split = chi::StringSplit(full_lua_name, "::");

  if (lua_name_split.size() == 1)
  {
    lua_pushcfunction(L, function_ptr);
    lua_setglobal(L, lua_name_split.back().c_str());
    return;
  }

  const std::vector<std::string> table_names(lua_name_split.begin(), lua_name_split.end() - 1);

  FleshOutLuaTableStructure(table_names);

  lua_pushstring(L, lua_name_split.back().c_str());
  lua_pushcfunction(L, function_ptr);
  lua_settable(L, -3);

  lua_pop(L, lua_gettop(L));
}

// ###################################################################
/**Sets/Forms a table structure that mimics the namespace structure of
 * a string. For example the string "sing::sob::nook::Tigger" will be
 * assigned a table structure
 * `sing.sob.nook.Tigger = "sing::sob::nook::Tigger"`. Then finally assigns
 * lua call to this table.*/
void
chi::Console::SetLuaFuncWrapperNamespaceTableStructure(const std::string& full_lua_name)
{
  auto L = GetInstance().console_state_;

  /**Lambda for making a chunk*/
  auto MakeChunk = [&L, &full_lua_name]()
  {
    std::string chunk_code = "local params = ...; ";
    chunk_code += "return chi_console.LuaWrapperCall(\"" + full_lua_name + "\", ...)";

    luaL_loadstring(L, chunk_code.c_str());
  };

  const auto table_names = chi::StringSplit(full_lua_name, "::");
  std::vector<std::string> namespace_names;
  for (const auto& table_name : table_names)
    if (table_name != table_names.back()) namespace_names.push_back(table_name);

  const auto& function_name = table_names.back();

  if (not namespace_names.empty())
  {
    FleshOutLuaTableStructure(namespace_names);
    lua_pushstring(L, function_name.c_str());
    MakeChunk();
    lua_settable(L, -3);
  }
  else
  {
    MakeChunk();
    lua_setglobal(L, function_name.c_str());
  }

  lua_pop(L, lua_gettop(L));
}

// ###################################################################
/**Sets/Forms a table structure that mimics the namespace structure of
 * a string. For example the string "sing::sob::nook::Tigger" will be
 * assigned a table structure
 * `sing.sob.nook.Tigger = "sing::sob::nook::Tigger"`.*/
void
chi::Console::SetObjectNamespaceTableStructure(const std::string& full_lua_name)
{
  auto L = GetInstance().console_state_;

  /**Lambda for registering object type and creation function.*/
  auto RegisterObjectItems = [&L](const std::string& full_name)
  {
    lua_pushstring(L, "type");
    lua_pushstring(L, full_name.c_str());
    lua_settable(L, -3);

    lua_pushstring(L, "Create");
    std::string chunk_code = "local params = ...; ";
    chunk_code += "return chiMakeObjectType(\"" + full_name + "\", ...)";

    luaL_loadstring(L, chunk_code.c_str());
    lua_settable(L, -3);
  };

  const auto table_names = chi::StringSplit(full_lua_name, "::");

  FleshOutLuaTableStructure(table_names);

  RegisterObjectItems(full_lua_name);

  lua_pop(L, lua_gettop(L));
}

// ##################################################################
/**Fleshes out a path in a table tree. For example, given
 * "fee::foo::fah::koo, this routine will make sure that
 * fee.foo.fah.koo is defined as a table tree structure. The routine will
 * create a table structure where one is needed and leave existing ones alone.
 *
 * At the end of the routine the last table in the structure will be on top
 * of the stack.*/
void
chi::Console::FleshOutLuaTableStructure(const std::vector<std::string>& table_names)
{
  auto L = GetInstance().console_state_;

  for (const auto& table_name : table_names)
  {
    // The first entry needs to be in lua's global scope,
    // so it looks a little different
    if (table_name == table_names.front())
    {
      lua_getglobal(L, table_name.c_str());
      if (not lua_istable(L, -1))
      {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, table_name.c_str());
        lua_getglobal(L, table_name.c_str());
      }
    }
    else
    {
      lua_getfield(L, -1, table_name.c_str());
      if (not lua_istable(L, -1))
      {
        lua_pop(L, 1);
        lua_pushstring(L, table_name.c_str());
        lua_newtable(L);
        lua_settable(L, -3);
        lua_getfield(L, -1, table_name.c_str());
      }
    }
  } // for table_key in table_keys
}

// ##################################################################
/**Sets a lua constant in the lua state.*/
void
chi::Console::SetLuaConstant(const std::string& constant_name, const chi_data_types::Varying& value)
{
  auto& console = GetInstance();
  auto L = console.console_state_;
  const auto path_names = chi::StringSplit(constant_name, "::");

  auto PushVaryingValue = [&L](const chi_data_types::Varying& var_value)
  {
    if (var_value.Type() == chi_data_types::VaryingDataType::BOOL)
      lua_pushboolean(L, var_value.BoolValue());
    else if (var_value.Type() == chi_data_types::VaryingDataType::STRING)
      lua_pushstring(L, var_value.StringValue().c_str());
    else if (var_value.Type() == chi_data_types::VaryingDataType::INTEGER)
      lua_pushinteger(L, static_cast<lua_Integer>(var_value.IntegerValue()));
    else if (var_value.Type() == chi_data_types::VaryingDataType::FLOAT)
      lua_pushnumber(L, var_value.FloatValue());
    else
      ChiInvalidArgument("Unsupported value type. Only bool, string, int and "
                         "double is supported");
  };

  if (path_names.size() == 1)
  {
    PushVaryingValue(value);
    lua_setglobal(L, path_names.front().c_str());
  }
  else
  {
    std::vector<std::string> namespace_names;
    for (const auto& table_name : path_names)
      if (table_name != path_names.back()) { namespace_names.push_back(table_name); }

    FleshOutLuaTableStructure(namespace_names);
    lua_pushstring(L, path_names.back().c_str());
    PushVaryingValue(value);
    lua_settable(L, -3);
  }

  lua_pop(L, lua_gettop(L));
}

// ##################################################################
/**Makes a formatted output, readible by the documentation scripts,
 * of all the lua wrapper functions.*/
void
chi::Console::DumpRegister() const
{
  Chi::log.Log() << "\n\n";
  for (const auto& [key, entry] : function_wrapper_registry_)
  {
    if (Chi::log.GetVerbosity() == 0)
    {
      Chi::log.Log() << key;
      continue;
    }

    Chi::log.Log() << "LUA_FUNCWRAPPER_BEGIN " << key;

    if (not entry.call_func) Chi::log.Log() << "SYNTAX_BLOCK";

    const auto in_params = entry.get_in_params_func();
    in_params.DumpParameters();

    Chi::log.Log() << "LUA_FUNCWRAPPER_END\n\n";
  }
  Chi::log.Log() << "\n\n";
}

void
Console::UpdateConsoleBindings(const chi::RegistryStatuses& old_statuses)
{
  auto ListHasValue = [](const std::vector<std::string>& list, const std::string& value)
  { return std::find(list.begin(), list.end(), value) != list.end(); };

  const auto& object_factory = ChiObjectFactory::GetInstance();
  for (const auto& [key, _] : object_factory.Registry())
    if (not ListHasValue(old_statuses.objfactory_keys_, key)) SetObjectNamespaceTableStructure(key);

  for (const auto& [key, entry] : lua_function_registry_)
    if (not ListHasValue(old_statuses.objfactory_keys_, key))
      SetLuaFuncNamespaceTableStructure(key, entry.function_ptr);

  for (const auto& [key, entry] : function_wrapper_registry_)
    if (not ListHasValue(old_statuses.objfactory_keys_, key))
      if (entry.call_func) SetLuaFuncWrapperNamespaceTableStructure(key);
}

} // namespace chi