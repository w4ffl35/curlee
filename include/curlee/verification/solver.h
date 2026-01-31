#pragma once

#include <optional>
#include <string>
#include <vector>
#include <z3++.h>

/**
 * @file solver.h
 * @brief Thin wrapper around Z3 used by the verification subsystem.
 */

namespace curlee::verification
{

/** @brief Check result from the solver. */
enum class CheckResult
{
    Sat,
    Unsat,
    Unknown,
};

/** @brief Single entry of a model (variable name and value as string). */
struct ModelEntry
{
    std::string name;
    std::string value;
};

/** @brief Model returned by the solver, as a collection of entries. */
struct Model
{
    std::vector<ModelEntry> entries;
};

/** @brief Solver wrapper exposing a minimal API used by the verifier. */
class Solver
{
  public:
    Solver();

    [[nodiscard]] z3::context& context();
    void add(const z3::expr& constraint);
    void push();
    void pop();
    [[nodiscard]] CheckResult check();
    [[nodiscard]] std::optional<Model> model_for(const std::vector<z3::expr>& vars) const;
    [[nodiscard]] static std::string format_model(const Model& model);

  private:
    z3::context ctx_;
    z3::solver solver_;
    std::optional<CheckResult> last_result_;
    std::optional<z3::model> last_model_;
};

} // namespace curlee::verification
