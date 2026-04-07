#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  if (table_info_ == nullptr) {
    throw OneBaseException("IndexScanExecutor::Init: table not found");
  }
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (iter_ == end_) {
    return false;
  }
  *tuple = *iter_;
  *rid = iter_.GetRID();
  ++iter_;
  return true;
}

}  // namespace onebase
