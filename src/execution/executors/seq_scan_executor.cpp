#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  if (table_info_ == nullptr) {
    throw OneBaseException("SeqScanExecutor::Init: table not found");
  }
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (iter_ != end_) {
    Tuple current = *iter_;
    RID current_rid = iter_.GetRID();
    ++iter_;

    if (plan_->GetPredicate() != nullptr) {
      const auto pred = plan_->GetPredicate()->Evaluate(&current, &table_info_->schema_);
      if (!pred.GetAsBoolean()) {
        continue;
      }
    }

    *tuple = current;
    *rid = current_rid;
    return true;
  }
  return false;
}

}  // namespace onebase
