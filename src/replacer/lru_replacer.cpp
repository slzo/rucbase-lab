#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }

LRUReplacer::~LRUReplacer() = default;

/**
 * @brief 使用LRU策略删除一个victim frame，这个函数能得到frame_id
 * @param[out] frame_id id of frame that was removed, nullptr if no victim was found
 * @return true if a victim frame was found, false otherwise
 */
bool LRUReplacer::Victim(frame_id_t *frame_id) {
    // C++17 std::scoped_lock
    // 它能够避免死锁发生，其构造函数能够自动进行上锁操作，析构函数会对互斥量进行解锁操作，保证线程安全。
    std::scoped_lock lock{latch_};

    // Todo:
    //  利用lru_replacer中的LRUlist_,LRUHash_实现LRU策略
    //  选择合适的frame指定为淘汰页面,赋值给*frame_id

    if(LRUlist_.empty()) //no victim
       return false;

    *frame_id = LRUlist_.back(); //choose the last element as the victim
    LRUlist_.pop_back(); //delete from the list
    LRUhash_.erase(*frame_id); //delete from the hash
    return true;
}

/**
 * @brief 固定一个frame, 表明它不应该成为victim（即在replacer中移除该frame_id）
 * @param frame_id the id of the frame to pin
 */
void LRUReplacer::Pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    // Todo:
    // 固定指定id的frame
    // 在数据结构中移除该frame

    if( LRUhash_.find(frame_id)== LRUhash_.end() ) //待pin的frame不存在
        return;

    LRUlist_.erase(LRUhash_[frame_id]);
    LRUhash_.erase(frame_id);
    return;
}

/**
 * 取消固定一个frame, 表明它可以成为victim（即将该frame_id添加到replacer）
 * @param frame_id the id of the frame to unpin
 */
void LRUReplacer::Unpin(frame_id_t frame_id) {
    // Todo:
    //  支持并发锁
    //  选择一个frame取消固定
    //  需考虑已经unpin的情况
    std::scoped_lock lock{latch_}; // lock
    if( LRUhash_.find(frame_id) != LRUhash_.end() ) //待Unpin的victim已经是unpin状态
        return;

    LRUlist_.push_front(frame_id); // insert to the head: just used
    LRUhash_[frame_id] = LRUlist_.begin(); //frame_id ---> list.begin
}

/** @return replacer中能够victim的数量 */
size_t LRUReplacer::Size() {
    // Todo:
    // 改写return size
    return LRUlist_.size();
}
