#include "backend_scalar_c.h"
#include <filesystem>
#include <system_error>
#include <cstdio>
#include <cerrno>
#include <cstdlib>

#include <dlfcn.h>

using namespace gigagrad;
using namespace gigagrad::codegen;

struct LowerCtx
{
    const char *prefix;
    FILE *file;
    int indentation;
};

static void Lower_ScalarC(LowerCtx &ctx, const LoadIntImmediateInsn &i, size_t iinsn, NumericDataType dtype)
{
    std::fprintf(ctx.file, "%*sint64_t v%zu = %" PRIi64 ";\n", ctx.indentation, " ", iinsn, i.value);
}

static void Lower_ScalarC(LowerCtx &ctx, const IntArithmeticInsn &i, size_t iinsn, NumericDataType dtype)
{
    std::fprintf(ctx.file,
                 "%*sint64_t v%zu = v%zu %c v%zu;\n", ctx.indentation, " ", iinsn, i.x, (char)i.op, i.y);
}

static void Lower_ScalarC(LowerCtx &ctx, const BeginLoopInsn &i, size_t iinsn, NumericDataType dtype)
{
    std::fprintf(ctx.file, "%*sfor(int64_t v%zu = 0; v%zu < %zd; v%zu++)\n%*s{\n",
                 ctx.indentation, " ", iinsn, iinsn, i.range, iinsn, ctx.indentation, " ");
    ctx.indentation += 4;
}

static void Lower_ScalarC(LowerCtx &ctx, const EndLoopInsn &i, size_t iinsn, NumericDataType dtype)
{
    ctx.indentation -= 4;
    std::fprintf(ctx.file, "%*s}\n", ctx.indentation, " ");
}

static void Lower_ScalarC(LowerCtx &ctx, const LoadInsn &i, size_t iinsn, NumericDataType dtype)
{
    std::fprintf(ctx.file, "%*s%s v%zu = i%zu[v%zu];\n",
                 ctx.indentation, " ", getCDatatype(dtype).c_str(), iinsn, i.input, i.idx);
}

static void Lower_ScalarC(LowerCtx &ctx, const StoreInsn &i, size_t iinsn, NumericDataType dtype)
{
    std::fprintf(ctx.file, "%*soutput[v%zu] = v%zu;\n", ctx.indentation, " ", i.offset, i.value);
}

static void Lower_ScalarC(LowerCtx &ctx, const LoadImmediateInsn &i, size_t iinsn, NumericDataType dtype)
{
    std::fprintf(ctx.file, "%*s%s v%zu = %f;\n", ctx.indentation, " ", getCDatatype(dtype).c_str(), iinsn, i.value);
}

static void Lower_ScalarC(LowerCtx &ctx, const UnaryInsn &i, size_t iinsn, NumericDataType dtype)
{
    auto op_str = i.type == UnaryOpType::EXP    ? "exp"
                  : i.type == UnaryOpType::LOG  ? "log"
                  : i.type == UnaryOpType::SIN  ? "sin"
                  : i.type == UnaryOpType::SQRT ? "sqrtf"
                                                : "INVALID";
    std::fprintf(ctx.file, "%*s%s v%zu = %s(v%zu);\n",
                 ctx.indentation, " ", getCDatatype(dtype).c_str(), iinsn, op_str, i.x);
}

static void Lower_ScalarC(LowerCtx &ctx, const BinaryInsn &i, size_t iinsn, NumericDataType dtype)
{

    if (i.type == BinaryOpType::ADD || i.type == BinaryOpType::SUB || i.type == BinaryOpType::MUL || i.type == BinaryOpType::DIV || i.type == BinaryOpType::CMP)
    {
        auto op_str = i.type == BinaryOpType::ADD   ? "+"
                      : i.type == BinaryOpType::SUB ? "-"
                      : i.type == BinaryOpType::MUL ? "*"
                      : i.type == BinaryOpType::DIV ? "/"
                                                    : "==";

        std::fprintf(ctx.file, "%*s%s v%zu = (%s)(v%zu %s v%zu);\n",
                     ctx.indentation, " ", getCDatatype(dtype).c_str(), iinsn, getCDatatype(dtype).c_str(), i.x, op_str, i.y);
    }
    else if (i.type == BinaryOpType::MAX)
    {
        std::fprintf(ctx.file, "%*s%s v%zu = v%zu > v%zu ? v%zu : v%zu;\n",
                     ctx.indentation, " ", getCDatatype(dtype).c_str(), iinsn, i.x, i.y, i.x, i.y);
    }
    else
    {
        std::fprintf(ctx.file, "%*s%s v%zu = pow(v%zu, v%zu);\n",
                     ctx.indentation, " ", getCDatatype(dtype).c_str(), iinsn, i.x, i.y);
    }
}

static void Lower_ScalarC(LowerCtx &ctx, const AccumulateInsn &i, size_t iinsn, NumericDataType dtype)
{
    if (i.type == ReduceOpType::MAX)
        std::fprintf(ctx.file,
                     "%*sv%zu = v%zu > v%zu ? v%zu : v%zu;\n",
                     ctx.indentation, " ", i.accumulator, i.accumulator, i.x, i.accumulator, i.x);
    else
        std::fprintf(ctx.file, "%*sv%zu += v%zu;\n", ctx.indentation, " ", i.accumulator, i.x);
}

static void Lower_ScalarC(LowerCtx &ctx, const FunctionBuilder &fn, size_t ifn, NumericDataType dtype)
{
    std::fprintf(ctx.file, "static void %s_%zu(\n", ctx.prefix, ifn);
    for (size_t i = 0; i < fn.inputs.size(); i++)
        std::fprintf(ctx.file, "    const %s *i%zu,\n", getCDatatype(dtype).c_str(), i);
    std::fprintf(ctx.file, "    %s *output)\n{\n", getCDatatype(dtype).c_str());
    ctx.indentation = 4;
    for (size_t i = 0; i < fn.insns.size(); i++)
    {
        std::visit([&](auto &&insn)
                   { Lower_ScalarC(ctx, insn, i, dtype); }, fn.insns[i]);
    }
    std::fprintf(ctx.file, "}\n\n");
}

static void GenerateMain(const Program &program, LowerCtx &ctx)
{
    std::fprintf(ctx.file, "void gigagrad_main(void **buffers)\n{\n");
    std::fprintf(ctx.file, "#if __linux__\n");
    std::fprintf(ctx.file, "    feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);\n");
    std::fprintf(ctx.file, "#endif\n");
    for (size_t ifn = 0; ifn < program.functions.size(); ifn++)
    {
        const FunctionBuilder &fn = program.functions[ifn];
        std::fprintf(ctx.file, "    %s_%zu(\n", ctx.prefix, ifn);
        for (size_t iinput = 0; iinput < fn.inputs.size(); iinput++)
            std::fprintf(ctx.file, "        buffers[%zu],\n", fn.inputs[iinput]);
        std::fprintf(ctx.file, "        buffers[%zu]);\n\n", fn.output_buffer);
    }
    std::fprintf(ctx.file, "}\n");
}

using GraphEvalFn = BackendScalarC::GraphEvalFn;

static std::pair<GraphEvalFn, void *> CompileAndLoad(const std::filesystem::path &source_path)
{
    std::filesystem::path obj_path = source_path;
    obj_path.replace_extension(".so");
    std::string command =
        "cc " +
        source_path.string() +
        " -o " +
        obj_path.string() +
        "  -Ofast -fPIC -shared -lm -march=native -mtune=native";
    std::system(command.c_str());
    // std::printf("Compiling with: %s\n", command.c_str());

    void *handle = dlopen(obj_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        throw std::runtime_error(dlerror());
    dlerror(); // Clear error conditions
    auto main_fn = reinterpret_cast<GraphEvalFn>(dlsym(handle, "gigagrad_main"));
    if (!main_fn)
    {
        char *err = dlerror();
        if (!err)
            throw std::runtime_error("Symbol gigagrad_main is NULL, which is unexpected");
        else
            throw std::runtime_error(err);
    }
    return {main_fn, handle};
}

static std::pair<GraphEvalFn, void *> Lower_ScalarC(const char *prefix, const Program &program, NumericDataType dtype)
{
    auto file_name = std::filesystem::temp_directory_path() / prefix;
    file_name += ".c";
    std::printf("FILE: %s\n", file_name.c_str());
    FILE *file = std::fopen(file_name.c_str(), "w+");
    if (!file)
        throw std::system_error(errno, std::generic_category());

    LowerCtx ctx = {prefix, file, 0};

    std::fprintf(file, "#define _GNU_SOURCE\n#include <fenv.h>\n");
    std::fprintf(file, "#include <stdint.h>\n#include <math.h>\n\n");

    for (size_t ifn = 0; ifn < program.functions.size(); ifn++)
        ::Lower_ScalarC(ctx, program.functions[ifn], ifn, dtype);

    GenerateMain(program, ctx);
    std::fclose(file);
    return CompileAndLoad(file_name);
}

BackendScalarC::~BackendScalarC()
{
    dlclose(this->handle);
    for (ssize_t ibuff = 0; ibuff < std::ssize(this->program.buffers); ibuff++)
    {
        auto &desc = this->program.buffers[ibuff];
        if (!std::holds_alternative<GraphNodeHandle>(desc.id))
        {
            delete[] reinterpret_cast<float *>(this->buffers[ibuff]);
        }
    }
}

void BackendScalarC::LowerProgram(Program &&program)
{
    this->program = std::move(program);
    auto [eval_fn, handle] = Lower_ScalarC("gg_scalar", this->program, NumericDataType::FLOAT32);
    this->eval_fn = eval_fn;
    this->handle = handle;
}

void *BackendScalarC::InitBuffers()
{
    this->buffers.reserve(this->program.buffers.size());
    for (ssize_t ibuff = 0; ibuff < std::ssize(this->program.buffers); ibuff++)
    {
        auto &desc = this->program.buffers[ibuff];
        if (std::holds_alternative<GraphNodeHandle>(desc.id))
        {
            GraphNodeHandle tensor = std::get<GraphNodeHandle>(desc.id);
            this->buffers.push_back(reinterpret_cast<void *>(tensor.data()));
        }
        else
        {
            float *intermediate_buf = new float[desc.size_elts];
            this->buffers.push_back(reinterpret_cast<void *>(intermediate_buf));
        }
    }
    return this->buffers[this->program.functions.back().output_buffer];
}

void *BackendScalarC::GetBuffer(size_t idx)
{
    return this->buffers.at(idx);
}

void BackendScalarC::Execute()
{
    for (ssize_t ibuff = 0; ibuff < std::ssize(this->program.buffers); ibuff++)
    {
        auto &desc = this->program.buffers[ibuff];
        if (std::holds_alternative<GraphNodeHandle>(desc.id))
        {
            GraphNodeHandle tensor = std::get<GraphNodeHandle>(desc.id);
            this->buffers[ibuff] = (reinterpret_cast<void *>(tensor.data()));
        }
    }
    eval_fn(this->buffers.data());
}
