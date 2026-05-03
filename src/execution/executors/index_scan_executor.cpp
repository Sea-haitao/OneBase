#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  auto *catalog = GetExecutorContext()->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());
  if (table_info_ == nullptr) {
    throw OneBaseException("IndexScanExecutor::Init: table not found");
  }
  index_info_ = catalog->GetIndex(plan_->GetIndexOid());
  if (index_info_ == nullptr) {
    throw OneBaseException("IndexScanExecutor::Init: index not found");
  }

  const int32_t key = plan_->GetLookupKey()->Evaluate(nullptr, nullptr).GetAsInteger();
  matching_rids_.clear();
  if (const auto *rids = index_info_->LookupInteger(key); rids != nullptr) {
    matching_rids_ = *rids;
  }
  cursor_ = 0;
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  const auto &schema = table_info_->schema_;
  while (cursor_ < matching_rids_.size()) {
    const RID current_rid = matching_rids_[cursor_++];
    Tuple raw = table_info_->table_->GetTuple(current_rid);

    std::vector<Value> values;
    values.reserve(schema.GetColumnCount());
    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
      values.push_back(raw.GetValue(&schema, i));
    }
    Tuple current(std::move(values));
    current.SetRID(current_rid);

    if (plan_->GetPredicate() != nullptr) {
      const auto pred = plan_->GetPredicate()->Evaluate(&current, &schema);
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
