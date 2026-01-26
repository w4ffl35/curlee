#include <algorithm>
#include <curlee/verification/solver.h>

namespace curlee::verification
{

Solver::Solver() : solver_(ctx_) {}

z3::context& Solver::context()
{
    return ctx_;
}

void Solver::add(const z3::expr& constraint)
{
    solver_.add(constraint);
}

void Solver::push()
{
    solver_.push();
    last_result_.reset();
    last_model_.reset();
}

void Solver::pop()
{
    solver_.pop();
    last_result_.reset();
    last_model_.reset();
}

CheckResult Solver::check()
{
    const auto res = solver_.check();
    switch (res)
    {
    case z3::sat:
        last_result_ = CheckResult::Sat;
        last_model_ = solver_.get_model();
        return CheckResult::Sat;
    case z3::unsat:
        last_result_ = CheckResult::Unsat;
        last_model_.reset();
        return CheckResult::Unsat;
    default:
        last_result_ = CheckResult::Unknown;
        last_model_.reset();
        return CheckResult::Unknown;
    }
}

std::optional<Model> Solver::model_for(const std::vector<z3::expr>& vars) const
{
    if (!last_result_.has_value() || *last_result_ != CheckResult::Sat)
    {
        return std::nullopt;
    }
    if (!last_model_.has_value())
    {
        return std::nullopt;
    }

    Model model;
    model.entries.reserve(vars.size());

    for (const auto& var : vars)
    {
        const auto name = var.decl().name().str();
        const auto value = last_model_->eval(var, true).to_string();
        model.entries.push_back({name, value});
    }

    return model;
}

std::string Solver::format_model(const Model& model)
{
    std::string out;
    auto entries = model.entries;
    std::sort(entries.begin(), entries.end(),
              [](const ModelEntry& a, const ModelEntry& b) { return a.name < b.name; });

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (i > 0)
        {
            out.append("\n");
        }
        out.append(entries[i].name);
        out.append(" = ");
        out.append(entries[i].value);
    }
    return out;
}

} // namespace curlee::verification
