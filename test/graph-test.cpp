#include <catch2/catch_test_macros.hpp>
#include "src/graph.h"
#include "src/codegen.h"
#include "src/backend_scalar_c.h"
#include "src/training.h"

#include <cmath>
#include <random>
#include <iostream>

namespace gg = gigagrad;

void TestGradient(
    gg::nn::Module &network,
    gg::GraphNodeHandle w,
    gg::GraphNodeHandle result,
    double expected)
{
    gg::TrainingContext ctx = gg::CompileTrainingGraph<gg::codegen::BackendScalarC>(network, result, 1.0);
    double example = 0.0;
    ctx.training_example = &example;
    ctx.Execute();
    std::cout << "w.data(): " << *w.data() << std::endl;
    std::cout << "expected: " << expected << std::endl;
    double pct_diff = std::abs(*w.data() - expected);
    REQUIRE(pct_diff < 0.001);
}

TEST_CASE("TestGradients_EXP", "[Train]")
{
    gg::nn::Module network;
    auto dtype = gg::NumericDataType::FLOAT32;
    auto w = network.AddWeight(1, dtype);
    auto result = exp(w);
    double w_data = 0.0;
    w.data() = &w_data;
    // ∂/∂w (E - exp(w))^2 = 2(E - exp(w)) * ∂/∂w(E - exp(w)) = 2(E - exp(w)) * -exp(w)
    // If E = 0, above equals 2exp(2w). If w = 0, above equals 2. So after gradient update,
    // w should be 0 - 2 = -2.
    TestGradient(network, w, result, -2.0);
}

TEST_CASE("TestGradients_LOG", "[Train]")
{
    gg::nn::Module network;
    auto w = network.AddWeight(1);
    auto result = log(w);
    double w_data = 1.0f;
    w.data() = &w_data;
    // ∂/∂w (E - log(w))^2 = 2(E - log(w)) * ∂/∂w(E - log(w)) = 2(E - log(w)) * -1/w
    // If E = 0, above equals log(w)/w. If w = 1, above equals 0. So after gradient update,
    // w should be 1 - 0 = 1.
    TestGradient(network, w, result, 1.0);
}

TEST_CASE("TestGradients_SIN", "[Train]")
{
    gg::nn::Module network;
    auto w = network.AddWeight(1);
    auto result = sin(w);
    double w_data = 0.0;
    w.data() = &w_data;
    // ∂/∂w (E - sin(w))^2 = 2(E - sin(w)) * ∂/∂w(E - sin(w)) = 2(E - sin(w)) * -cos(w)
    // If E = 0, above equals 2sin(w)cos(w). If w = 0, above equals 0. So after gradient update,
    // w should be 0 - 0 = 0.
    TestGradient(network, w, result, 0.0);
}

TEST_CASE("TestGradients_SQRT", "[Train]")
{
    gg::nn::Module network;
    auto w = network.AddWeight(1);
    auto result = sqrt(w);
    double w_data = 1.0;
    w.data() = &w_data;
    // ∂/∂w (E - sqrt(w))^2 = 2(E - sqrt(w)) * ∂/∂w(E - sqrt(w)) = 2(E - sqrt(w)) * (0 - 1/2 * 1/sqrt(w))
    // If E = 0, above equals -2sqrt(w)/-2sqrt(w). If w = 1, above equals 1. So after gradient update,
    // w should be 1 - 1 = 0.
    TestGradient(network, w, result, 0.0);
}

TEST_CASE("TestGradients_ADD", "[Train]")
{
    gg::nn::Module network;
    auto x = network.AddInput(1);
    auto w = network.AddWeight(1);
    auto result = x + w;
    double x_data = 1.0;
    double w_data = 1.0;
    x.data() = &x_data;
    w.data() = &w_data;
    // ∂/∂w (E - (x + w))^2 = 2(E - x - w) * ∂/∂w(E - x - w) = 2(E - x - w) * (0 - 0 - 1) = -2(E - x - w)
    // If E = 0, above equals 2(x + w). If x,w = 1, above equals 4. So after gradient update,
    // w should be 1 - 4 = -3.0f.
    TestGradient(network, w, result, -3.0);
}

TEST_CASE("TestGradients_SUB", "[Train]")
{
    gg::nn::Module network;
    auto x = network.AddInput(1, gg::NumericDataType::FLOAT64);
    auto w = network.AddWeight(1, gg::NumericDataType::FLOAT64);
    auto result = x - w;
    double x_data = 0.0;
    double w_data = 1.0;
    x.data() = &x_data;
    w.data() = &w_data;
    // ∂/∂w (E - (x - w))^2 = 2(E - x + w) * ∂/∂w(E - x + w) = 2(E - x + w) * (0 - 0 + 1) = 2(E - x + w)
    // If E = 0, above equals 2(-x + w). If x = 0, w = 1, above equals 2. So after gradient update,
    // w should be 1 - 2 = -1.0f.
    TestGradient(network, w, result, -1.0);
}

TEST_CASE("TestTrainSimple", "[Train]")
{
    gg::nn::Module network;
    auto x = network.AddInput(4);
    auto w = network.AddWeight(4);
    auto L1 = w - x;
    gg::TrainingContext ctx = gg::CompileTrainingGraph<gg::codegen::BackendScalarC>(network, L1);
    double x_data[] = {1.0, 2.0, 3.0, 4.0};
    double w_data[] = {-0.1, 0.1, -0.001, 0.0001};
    double training_example_data[] = {0.0, 0.0, 0.0, 0.0};
    x.data() = x_data;
    w.data() = w_data;
    ctx.training_example = training_example_data;
    double prev_loss = 1000;
    for (int i = 0; i < 50; i++)
    {
        ctx.Execute();
        REQUIRE(*ctx.loss < prev_loss);
    }
    for (int i = 0; i < 4; i++)
    {
        double pct_diff = (std::abs(w_data[i] - x_data[i]) / x_data[i]) * 100.0;
        REQUIRE(pct_diff < 1);
    }
}

TEST_CASE("TestXor", "[Codegen]")
{
    gg::Graph graph;
    auto x = graph.AddInput(2);
    auto w1 = graph.AddInput({2, 2});
    auto w2 = graph.AddInput({1, 2});
    auto b1 = graph.AddInput({2, 1});
    auto L1 = (w1 % x) > b1;
    auto L2 = (w2 % L1) > 1.5;
    auto result = L2.Compile<gg::codegen::BackendScalarC>();

    REQUIRE(L1.shape() == gg::Shape{2, 1});
    REQUIRE(L2.shape() == gg::Shape{1, 1});

    double x_data[] = {1.0, 1.0};
    double w1_data[] = {1.0, 1.0, -1.0, -1.0};
    double b1_data[] = {0.5, -1.5};
    double w2_data[] = {1.0, 1.0};
    x.data() = x_data;
    w1.data() = w1_data;
    b1.data() = b1_data;
    w2.data() = w2_data;

    for (bool x1 : {false, true})
    {
        x_data[0] = x1 ? 1.0 : 0.0;
        for (bool x2 : {false, true})
        {
            x_data[1] = x2 ? 1.0f : 0.0;
            result.Execute();

            double expected = (x1 ^ x2) ? 1.0 : 0.0;
            REQUIRE(result.data[0] == expected);
        }
    }
}

static std::default_random_engine Gen(0);

void RandomMatrix(double *m, size_t size_elts)
{
    std::uniform_real_distribution<double> dist(-2.0, 2.0);
    for (size_t i = 0; i < size_elts; i++)
        m[i] = dist(Gen);
}

void NaiveMatmul(double *x, double *y, size_t A, size_t B, size_t C, double *result)
{
    for (size_t irow = 0; irow < A; irow++)
    {
        double *row = x + B * irow;
        for (size_t icol = 0; icol < C; icol++)
        {
            double res = 0.0;

            double *col = y + icol;
            for (size_t i = 0; i < B; i++)
            {
                res += row[i] * col[C * i];
            }
            result[C * irow + icol] = res;
        }
    }
}

TEST_CASE("TestMatmul", "[Codegen]")
{
    constexpr size_t NumTrials = 10;
    for (size_t itrial = 0; itrial < NumTrials; itrial++)
    {
        std::uniform_int_distribution<gg::dim_t> dim_dist(1, 128);
        gg::dim_t A = dim_dist(Gen);
        gg::dim_t B = dim_dist(Gen);
        gg::dim_t C = dim_dist(Gen);
        std::printf("Trial %zu: (%zu x %zu) * (%zu x %zu)\n", itrial, A, B, B, C);

        gg::Graph graph;
        auto x = graph.AddInput({A, B});
        auto y = graph.AddInput({B, C});
        auto result = (x % y).Compile<gg::codegen::BackendScalarC>();

        x.data() = new double[A * B];
        y.data() = new double[B * C];
        RandomMatrix(x.data(), A * B);
        RandomMatrix(y.data(), B * C);

        result.Execute();
        auto actual = result.data;
        auto expected = new double[A * C];
        NaiveMatmul(x.data(), y.data(), A, B, C, expected);
        for (gg::dim_t i = 0; i < A * C; i++)
        {
            REQUIRE(std::abs(actual[i] - expected[i]) / actual[i] <= 0.02f);
        }
        // Make LeakSanitizer happy
        delete[] x.data();
        delete[] y.data();
        delete[] expected;
    }
}

TEST_CASE("TestLogisticRegressionShape", "[Graph]")
{
    gg::Graph graph;
    auto x = graph.AddInput({28, 28}).reshape({28 * 28, 1});
    auto w1 = graph.AddInput({800, 28 * 28});
    auto b1 = graph.AddInput({800, 1});
    auto z1 = (w1 % x) + b1;
    auto a2 = gg::sigmoid(z1);
    auto w2 = graph.AddInput({10, 800});
    auto b2 = graph.AddInput({10, 1});
    auto result = (w2 % a2) + b2;
    REQUIRE(x.shape() == gg::Shape{28 * 28, 1});
    REQUIRE(w1.shape() == gg::Shape{800, 28 * 28});
    REQUIRE(b1.shape() == gg::Shape{800, 1});
    REQUIRE(z1.shape() == gg::Shape{800, 1});
    REQUIRE(a2.shape() == gg::Shape{800, 1});
    REQUIRE(w2.shape() == gg::Shape{10, 800});
    REQUIRE(b2.shape() == gg::Shape{10, 1});
    REQUIRE(result.shape() == gg::Shape{10, 1});
}

TEST_CASE("TestSimpleGraphShape", "[Graph]")
{
    gigagrad::Graph graph;
    auto tensor1 = graph.AddInput({2, 2});
    auto tensor2 = graph.AddInput({2, 2});
    auto addition = tensor1 + tensor2;

    REQUIRE(addition->Kind() == gg::GraphNode::Kind::BinaryOp);
}
