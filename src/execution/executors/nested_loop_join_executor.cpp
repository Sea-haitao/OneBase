#include "onebase/execution/executors/nested_loop_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx,
                                                const NestedLoopJoinPlanNode *plan,
                                                std::unique_ptr<AbstractExecutor> left_executor,
                                                std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> right_tuples;
  Tuple right_tuple;
  RID right_rid;
  while (right_executor_->Next(&right_tuple, &right_rid)) {
    right_tuples.push_back(right_tuple);
  }

  Tuple left_tuple;
  RID left_rid;
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();
  while (left_executor_->Next(&left_tuple, &left_rid)) {
    for (const auto &rt : right_tuples) {
      bool match = true;
      if (plan_->GetPredicate() != nullptr) {
        match = plan_->GetPredicate()->EvaluateJoin(&left_tuple, &left_schema, &rt, &right_schema).GetAsBoolean();
      }
      if (!match) {
        continue;
      }
      std::vector<Value> values;
      values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());
      for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
        values.push_back(left_tuple.GetValue(&left_schema, i));
      }
      for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
        values.push_back(rt.GetValue(&right_schema, i));
      }
      result_tuples_.emplace_back(std::move(values));
    }
  }
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  *rid = RID();
  return true;
}

}  // namespace onebase
