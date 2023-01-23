#include "lock_manager.h"

#define GroupMode(id) lock_table_[id].group_lock_mode_
/**
 * 申请行级读锁
 * @param txn 要申请锁的事务对象指针
 * @param rid 加锁的目标记录ID
 * @param tab_fd 记录所在的表的fd
 * @return 返回加锁是否成功
 */
bool LockManager::LockSharedOnRecord(Transaction *txn, const Rid &rid, int tab_fd) {
    // Todo:
    // 1. 通过mutex申请访问全局锁表
    // 2. 检查事务的状态
    // 3. 查找当前事务是否已经申请了目标数据项上的锁，如果存在则根据锁类型进行操作，否则执行下一步操作
    // 4. 将要申请的锁放入到全局锁表中，并通过组模式来判断是否可以成功授予锁
    // 5. 如果成功，更新目标数据项在全局锁表中的信息，否则阻塞当前操作
    // 提示：步骤5中的阻塞操作可以通过条件变量来完成，所有加锁操作都遵循上述步骤，在下面的加锁操作中不再进行注释提示

    std::unique_lock<std::mutex> lock{latch_}; // 1

    if( txn->GetIsolationLevel()==IsolationLevel::READ_UNCOMMITTED || // 2
        txn->GetState()==TransactionState::SHRINKING
    )
        txn->SetState(TransactionState::ABORTED);
    if(txn->GetState()==TransactionState::ABORTED)
        return false;

    txn->SetState(TransactionState::GROWING);
    LockDataId newid(tab_fd, rid, LockDataType::RECORD);

    if (txn->GetLockSet()->find(newid)!=txn->GetLockSet()->end()) { // 3
        for (auto i=lock_table_[newid].request_queue_.begin(); i!=lock_table_[newid].request_queue_.end(); i++)
            if (i->txn_id_ == txn->GetTransactionId())
                lock_table_[newid].cv_.notify_all();
        lock.unlock();
        return true;
    }
    txn->GetLockSet()->insert(newid); // 4.1 put into lock_table
    LockRequest *newquest = new LockRequest(txn->GetTransactionId(), LockMode::SHARED);
    lock_table_[newid].request_queue_.push_back(*newquest);
    lock_table_[newid].shared_lock_num_++;

    while( GroupMode(newid) != GroupLockMode::S &&
           GroupMode(newid) != GroupLockMode::IS &&
           GroupMode(newid) != GroupLockMode::NON_LOCK ) // 4.2&5 group mode judgement
        lock_table_[newid].cv_.wait(lock);
    newquest->granted_ = true;
    lock_table_[newid].cv_.notify_all();
    lock.unlock();
    return true;
}

/**
 * 申请行级写锁
 * @param txn 要申请锁的事务对象指针
 * @param rid 加锁的目标记录ID
 * @param tab_fd 记录所在的表的fd
 * @return 返回加锁是否成功
 */
bool LockManager::LockExclusiveOnRecord(Transaction *txn, const Rid &rid, int tab_fd) {
    std::unique_lock<std::mutex> lock{latch_}; // 1

    if( txn->GetIsolationLevel()==IsolationLevel::READ_UNCOMMITTED || // 2
        txn->GetState()==TransactionState::SHRINKING
    )
        txn->SetState(TransactionState::ABORTED);
    if(txn->GetState()==TransactionState::ABORTED)
        return false;

    txn->SetState(TransactionState::GROWING);
    LockDataId newid(tab_fd, rid, LockDataType::RECORD);

    if (txn->GetLockSet()->find(newid)!=txn->GetLockSet()->end()) { // 3
        for (auto i=lock_table_[newid].request_queue_.begin(); i!=lock_table_[newid].request_queue_.end(); i++)
            if (i->txn_id_ == txn->GetTransactionId()) {
                GroupMode(newid) = GroupLockMode::X;
                lock_table_[newid].cv_.notify_all();
            }
        lock.unlock();
        return true;
    }
    txn->GetLockSet()->insert(newid); // 4.1 put into lock_table
    LockRequest *newquest = new LockRequest(txn->GetTransactionId(), LockMode::SHARED);
    lock_table_[newid].request_queue_.push_back(*newquest);
    lock_table_[newid].shared_lock_num_++;

    while(GroupMode(newid)!= GroupLockMode::NON_LOCK) // 4.2&5 group mode judgement
        lock_table_[newid].cv_.wait(lock);
    newquest->granted_ = true;
    GroupMode(newid) = GroupLockMode::X;
    lock_table_[newid].cv_.notify_all();
    lock.unlock();
    return true;
}

/**
 * 申请表级读锁
 * @param txn 要申请锁的事务对象指针
 * @param tab_fd 目标表的fd
 * @return 返回加锁是否成功
 */
bool LockManager::LockSharedOnTable(Transaction *txn, int tab_fd) {
    std::unique_lock<std::mutex> lock{latch_};

    if( txn->GetIsolationLevel()==IsolationLevel::READ_UNCOMMITTED ||
        txn->GetState()==TransactionState::SHRINKING
    )
        txn->SetState(TransactionState::ABORTED);
    if(txn->GetState()==TransactionState::ABORTED)
        return false;

    txn->SetState(TransactionState::GROWING);
    LockDataId newid(tab_fd, LockDataType::TABLE);

    if (txn->GetLockSet()->find(newid)!=txn->GetLockSet()->end()) {
        for (auto i=lock_table_[newid].request_queue_.begin(); i!=lock_table_[newid].request_queue_.end(); i++)
            if (i->txn_id_ == txn->GetTransactionId()) {
                if (GroupMode(newid) == GroupLockMode::IX)
                    GroupMode(newid) = GroupLockMode::SIX;
                else if (GroupMode(newid)==GroupLockMode::IS || GroupMode(newid)==GroupLockMode::NON_LOCK)
                    GroupMode(newid) = GroupLockMode::S;
                lock_table_[newid].cv_.notify_all();
            }
        lock.unlock();
        return true;
    }
    txn->GetLockSet()->insert(newid);
    LockRequest *newquest = new LockRequest(txn->GetTransactionId(), LockMode::SHARED);
    lock_table_[newid].request_queue_.push_back(*newquest);
    lock_table_[newid].shared_lock_num_++;

    while( GroupMode(newid)!=GroupLockMode::S &&
           GroupMode(newid)!=GroupLockMode::IS &&
           GroupMode(newid)!=GroupLockMode::NON_LOCK)
        lock_table_[newid].cv_.wait(lock);
    newquest->granted_ = true;
    if (GroupMode(newid) == GroupLockMode::IX)
        GroupMode(newid) = GroupLockMode::SIX;
    else if (GroupMode(newid)==GroupLockMode::IS || GroupMode(newid)==GroupLockMode::NON_LOCK)
        GroupMode(newid) = GroupLockMode::S;
    lock_table_[newid].cv_.notify_all();
    lock.unlock();
    return true;
}

/**
 * 申请表级写锁
 * @param txn 要申请锁的事务对象指针
 * @param tab_fd 目标表的fd
 * @return 返回加锁是否成功
 */
bool LockManager::LockExclusiveOnTable(Transaction *txn, int tab_fd) {
    std::unique_lock<std::mutex> lock{latch_}; // 1

    if( txn->GetIsolationLevel()==IsolationLevel::READ_UNCOMMITTED || // 2
        txn->GetState()==TransactionState::SHRINKING
    )
        txn->SetState(TransactionState::ABORTED);
    if(txn->GetState()==TransactionState::ABORTED)
        return false;

    txn->SetState(TransactionState::GROWING);
    LockDataId newid(tab_fd, LockDataType::TABLE);

    if (txn->GetLockSet()->find(newid)!=txn->GetLockSet()->end()) { // 3
        for (auto i=lock_table_[newid].request_queue_.begin(); i!=lock_table_[newid].request_queue_.end(); i++)
            if (i->txn_id_ == txn->GetTransactionId()) {
                GroupMode(newid) = GroupLockMode::X;
                lock_table_[newid].cv_.notify_all();
            }
        lock.unlock();
        return true;
    }
    txn->GetLockSet()->insert(newid); // 4.1 put into lock_table
    LockRequest *newquest = new LockRequest(txn->GetTransactionId(), LockMode::SHARED);
    lock_table_[newid].request_queue_.push_back(*newquest);
    lock_table_[newid].shared_lock_num_++;

    while(GroupMode(newid)!=GroupLockMode::NON_LOCK) // 4.2&5 group mode judgement
        lock_table_[newid].cv_.wait(lock);
    newquest->granted_ = true;
    GroupMode(newid) = GroupLockMode::X;
    lock_table_[newid].cv_.notify_all();
    lock.unlock();
    return true;
}

/**
 * 申请表级意向读锁
 * @param txn 要申请锁的事务对象指针
 * @param tab_fd 目标表的fd
 * @return 返回加锁是否成功
 */
bool LockManager::LockISOnTable(Transaction *txn, int tab_fd) {
    std::unique_lock<std::mutex> lock{latch_}; // 1

    if( txn->GetIsolationLevel()==IsolationLevel::READ_UNCOMMITTED || // 2
        txn->GetState()==TransactionState::SHRINKING
    )
        txn->SetState(TransactionState::ABORTED);
    if(txn->GetState()==TransactionState::ABORTED)
        return false;

    txn->SetState(TransactionState::GROWING);
    LockDataId newid(tab_fd, LockDataType::TABLE);

    if (txn->GetLockSet()->find(newid)!=txn->GetLockSet()->end()) { // 3
        for (auto i=lock_table_[newid].request_queue_.begin(); i!=lock_table_[newid].request_queue_.end(); i++)
            if (i->txn_id_ == txn->GetTransactionId()) {
                if( GroupMode(newid)==GroupLockMode::NON_LOCK)
                    GroupMode(newid) = GroupLockMode::IS;
                lock_table_[newid].cv_.notify_all();
            }
        lock.unlock();
        return true;
    }
    txn->GetLockSet()->insert(newid); // 4.1 put into lock_table
    LockRequest *newquest = new LockRequest(txn->GetTransactionId(), LockMode::SHARED);
    lock_table_[newid].request_queue_.push_back(*newquest);
    lock_table_[newid].shared_lock_num_++;

    while(GroupMode(newid)==GroupLockMode::X) // 4.2&5 group mode judgement
        lock_table_[newid].cv_.wait(lock);
    newquest->granted_ = true;
    if( GroupMode(newid)==GroupLockMode::NON_LOCK)
        GroupMode(newid) = GroupLockMode::IS;
    lock_table_[newid].cv_.notify_all();
    lock.unlock();
    return true;
}

/**
 * 申请表级意向写锁
 * @param txn 要申请锁的事务对象指针
 * @param tab_fd 目标表的fd
 * @return 返回加锁是否成功
 */
bool LockManager::LockIXOnTable(Transaction *txn, int tab_fd) {
    std::unique_lock<std::mutex> lock{latch_}; // 1

    if( txn->GetIsolationLevel()==IsolationLevel::READ_UNCOMMITTED || // 2
        txn->GetState()==TransactionState::SHRINKING
    )
        txn->SetState(TransactionState::ABORTED);
    if(txn->GetState()==TransactionState::ABORTED)
        return false;

    txn->SetState(TransactionState::GROWING);
    LockDataId newid(tab_fd, LockDataType::TABLE);

    if (txn->GetLockSet()->find(newid)!=txn->GetLockSet()->end()) { // 3
        for (auto i=lock_table_[newid].request_queue_.begin(); i!=lock_table_[newid].request_queue_.end(); i++)
            if (i->txn_id_ == txn->GetTransactionId()) {
                if(GroupMode(newid)==GroupLockMode::S)
                    GroupMode(newid) = GroupLockMode::SIX;
                else if(GroupMode(newid)==GroupLockMode::IS || GroupMode(newid)==GroupLockMode::NON_LOCK)
                    GroupMode(newid) = GroupLockMode::IX;
                lock_table_[newid].cv_.notify_all();
            }
        lock.unlock();
        return true;
    }
    txn->GetLockSet()->insert(newid); // 4.1 put into lock_table
    LockRequest *newquest = new LockRequest(txn->GetTransactionId(), LockMode::SHARED);
    lock_table_[newid].request_queue_.push_back(*newquest);
    lock_table_[newid].shared_lock_num_++;

    while(GroupMode(newid)==GroupLockMode::X &&
           GroupMode(newid)==GroupLockMode::S &&
           GroupMode(newid)==GroupLockMode::SIX) // 4.2&5 group mode judgement
        lock_table_[newid].cv_.wait(lock);
    newquest->granted_ = true;
    if(GroupMode(newid)==GroupLockMode::S)
        GroupMode(newid) = GroupLockMode::SIX;
    else if(GroupMode(newid)==GroupLockMode::IS || GroupMode(newid)==GroupLockMode::NON_LOCK)
        GroupMode(newid) = GroupLockMode::IX;
    lock_table_[newid].cv_.notify_all();
    lock.unlock();
    return true;
}

/**
 * 释放锁
 * @param txn 要释放锁的事务对象指针
 * @param lock_data_id 要释放的锁ID
 * @return 返回解锁是否成功
 */
bool LockManager::Unlock(Transaction *txn, LockDataId lock_data_id) {
    std::unique_lock<std::mutex> lock(latch_);
    txn->SetState(TransactionState::SHRINKING);
    if (txn->GetLockSet()->find(lock_data_id) != txn->GetLockSet()->end()) {
        GroupLockMode mode = GroupLockMode::NON_LOCK;
        for(auto i=lock_table_[lock_data_id].request_queue_.begin(); i!=lock_table_[lock_data_id].request_queue_.end(); i++) {
            if(i->granted_) {
                if(i->lock_mode_ == LockMode::EXLUCSIVE)
                    mode = GroupLockMode::X;
                else if(i->lock_mode_==LockMode::SHARED && mode!=GroupLockMode::SIX)
                    mode = mode==GroupLockMode::IX ? GroupLockMode::SIX : GroupLockMode::S;
                else if(i->lock_mode_ == LockMode::S_IX)
                    mode = GroupLockMode::SIX;
                else if(i->lock_mode_==LockMode::INTENTION_EXCLUSIVE && mode!= GroupLockMode::SIX)
                    mode = mode==GroupLockMode::S ? GroupLockMode::SIX : GroupLockMode::IX;
                else if(i->lock_mode_ == LockMode::INTENTION_SHARED)
                    mode = mode==GroupLockMode::NON_LOCK ? GroupLockMode::IS :
                          mode==GroupLockMode::IS ? GroupLockMode::IS : mode ;
            }
        }
        lock_table_[lock_data_id].group_lock_mode_ = mode;
        lock_table_[lock_data_id].cv_.notify_all();
        lock.unlock();
        return true;
    }
    else {
        lock.unlock();
        return false;
    }
}
