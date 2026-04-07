#include "onebase/execution/executors/aggregation_executor.h"
#include "onebase/common/exception.h"
#include "onebase/type/type_id.h"
#include "onebase/type/value.h"

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  child_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  struct AggGroupState {
    std::vector<Value> group_values;
    std::vector<Value> agg_values;
  };

  auto make_default_aggs = [&]() -> std::vector<Value> {
    std::vector<Value> defaults;
    defaults.reserve(plan_->GetAggregateTypes().size());
    for (size_t i = 0; i < plan_->GetAggregateTypes().size(); i++) {
      auto agg_type = plan_->GetAggregateTypes()[i];
      if (agg_type == AggregationType::CountStarAggregate || agg_type == AggregationType::CountAggregate) {
        defaults.emplace_back(TypeId::INTEGER, 0);
      } else {
        defaults.emplace_back(plan_->GetAggregates()[i]->GetReturnType());
      }
    }
    return defaults;
  };

  std::unordered_map<std::string, AggGroupState> groups;
  Tuple child_tuple;
  RID child_rid;
  const auto &child_schema = child_executor_->GetOutputSchema();
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> group_vals;
    group_vals.reserve(plan_->GetGroupBys().size());
    std::string group_key;
    for (const auto &expr : plan_->GetGroupBys()) {
      Value v = expr->Evaluate(&child_tuple, &child_schema);
      group_vals.push_back(v);
      group_key.append(v.ToString()).append("|");
    }

    auto it = groups.find(group_key);
    if (it == groups.end()) {
      AggGroupState state;
      state.group_values = group_vals;
      state.agg_values = make_default_aggs();
      it = groups.emplace(group_key, std::move(state)).first;
    }

    for (size_t i = 0; i < plan_->GetAggregateTypes().size(); i++) {
      auto agg_type = plan_->GetAggregateTypes()[i];
      Value agg_input = plan_->GetAggregates()[i]->Evaluate(&child_tuple, &child_schema);
      auto &current = it->second.agg_values[i];
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
          current = Value(TypeId::INTEGER, current.GetAsInteger() + 1);
          break;
        case AggregationType::CountAggregate:
          if (!agg_input.IsNull()) {
            current = Value(TypeId::INTEGER, current.GetAsInteger() + 1);
          }
          break;
        case AggregationType::SumAggregate:
          if (!agg_input.IsNull()) {
            current = current.IsNull() ? agg_input : current.Add(agg_input);
          }
          break;
        case AggregationType::MinAggregate:
          if (!agg_input.IsNull()) {
            if (current.IsNull() || agg_input.CompareLessThan(current).GetAsBoolean()) {
              current = agg_input;
            }
          }
          break;
        case AggregationType::MaxAggregate:
          if (!agg_input.IsNull()) {
            if (current.IsNull() || agg_input.CompareGreaterThan(current).GetAsBoolean()) {
              current = agg_input;
            }
          }
          break;
      }
    }
  }

  for (auto &kv : groups) {
    std::vector<Value> out_vals = kv.second.group_values;
    out_vals.insert(out_vals.end(), kv.second.agg_values.begin(), kv.second.agg_values.end());
    result_tuples_.emplace_back(std::move(out_vals));
  }

  if (groups.empty() && plan_->GetGroupBys().empty()) {
    result_tuples_.emplace_back(make_default_aggs());
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  *rid = RID();
  return true;
}

}  // namespace onebase
