#pragma once

#include <optional>
#include <string>
#include <vector>
#include <z3++.h>

namespace curlee::verification
{

enum class CheckResult
{
    Sat,
    Unsat,
    Unknown,
};

struct ModelEntry
{
    std::string name;
    std::string value;
};

struct Model
{
    std::vector<ModelEntry> entries;
};

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
