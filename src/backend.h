#pragma once
#include "codegen.h"

namespace Gigagrad
{
namespace Codegen
{

enum class Backend
{
    ScalarC,
};

using GraphEvalFn = void (*)(void **);

// TODO: Do we need to support user-provided code generators? Should we accept some sort of
//       class with virtual methods to codegen things? 
GraphEvalFn LowerProgram(const char *prefix, Backend backend, const Program &program);
}
}