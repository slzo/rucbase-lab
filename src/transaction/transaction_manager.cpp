#include "transaction_manager.h"
#include "record/rm_file_handle.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

/**
 * 事务的开始方法
 * @param txn 事务指针
 * @param log_manager 日志管理器，用于日志lab
 * @return 当前事务指针
 * @tips: 事务的指针可能为空指针
 */
Transaction * TransactionManager::Begin(Transaction *txn, LogManager *log_manager) {
    // Todo:
    // 1. 判断传入事务参数是否为空指针
    // 2. 如果为空指针，创建新事务
    // 3. 把开始事务加入到全局事务表中
    // 4. 返回当前事务指针
    if( !txn ) { // 2
        txn = new Transaction(next_txn_id_, IsolationLevel::SERIALIZABLE);
        next_txn_id_ +=1 ;
        txn->SetState(TransactionState::DEFAULT);
    }
    txn_map[txn->GetTransactionId()] = txn; // 3
    return txn; // 4
}

/**
 * 事务的提交方法
 * @param txn 事务指针
 * @param log_manager 日志管理器，用于日志lab
 * @param sm_manager 系统管理器，用于commit，后续会删掉
 */
void TransactionManager::Commit(Transaction * txn, LogManager *log_manager) {
    // Todo:
    // 1. 如果存在未提交的写操作，提交所有的写操作
    // 2. 释放所有锁
    // 3. 释放事务相关资源，eg.锁集
    // 4. 更新事务状态

    auto wset = txn->GetWriteSet();
    while( !wset->empty() ) // 1
        wset->pop_back();

    auto lset = txn->GetLockSet();
    for(auto i = lset->begin(); i != lset->end(); i++ ) // 2
        lock_manager_->Unlock(txn, *i);
    lset->clear();

    txn->SetState(TransactionState::COMMITTED); // 4
}

/**
 * 事务的终止方法
 * @param txn 事务指针
 * @param log_manager 日志管理器，用于日志lab
 * @param sm_manager 系统管理器，用于rollback，后续会删掉
 */
void TransactionManager::Abort(Transaction * txn, LogManager *log_manager) {
    // Todo:
    // 1. 回滚所有写操作
    // 2. 释放所有锁
    // 3. 清空事务相关资源，eg.锁集
    // 4. 更新事务状态
    auto wset = txn->GetWriteSet();
    while( !wset->empty() ) { // 1
        Context* ctx = new Context(lock_manager_, log_manager, txn);
        if( wset->back()->GetWriteType() == WType::INSERT_TUPLE )
            sm_manager_->rollback_insert(wset->back()->GetTableName(), wset->back()->GetRid(), ctx);
        else if( wset->back()->GetWriteType() == WType::DELETE_TUPLE )
            sm_manager_->rollback_delete(wset->back()->GetTableName(), wset->back()->GetRecord(), ctx);
        else if( wset->back()->GetWriteType() == WType::UPDATE_TUPLE )
            sm_manager_->rollback_update(wset->back()->GetTableName(), wset->back()->GetRid(), wset->back()->GetRecord(), ctx);
        wset->pop_back();
    }

    auto lset = txn->GetLockSet();
    for(auto i = lset->begin(); i != lset->end(); i++ ) // 2
        lock_manager_->Unlock(txn, *i);
    lset->clear();

    txn->SetState(TransactionState::ABORTED); // 4

}

/** 以下函数用于日志实验中的checkpoint */
void TransactionManager::BlockAllTransactions() {}

void TransactionManager::ResumeAllTransactions() {}
