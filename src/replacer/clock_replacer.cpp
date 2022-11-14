#include "replacer/clock_replacer.h"

#include <algorithm>

ClockReplacer::ClockReplacer(size_t num_pages)
    : circular_{num_pages, ClockReplacer::Status::EMPTY_OR_PINNED}, hand_{0}, capacity_{num_pages} {
    // 成员初始化列表语法
    circular_.reserve(num_pages);
}

ClockReplacer::~ClockReplacer() = default;

bool ClockReplacer::Victim(frame_id_t *frame_id) {
    if ( !Size() )
        return false;
    const std::lock_guard<mutex_t> guard(mutex_);
    // Todo: try to find a victim frame in buffer pool with clock scheme
    // and make the *frame_id = victim_frame_id
    // not found, frame_id=nullptr and return false
    while( 1 ) {
        hand_++;
        hand_ %= capacity_;
        if( circular_[hand_] == Status::UNTOUCHED ) {
            circular_[hand_] = Status::EMPTY_OR_PINNED;
            *frame_id = hand_;
            return true;
        }
        if( circular_[hand_] == Status:: ACCESSED )
            circular_[hand_] = Status::UNTOUCHED;
    }
    return false;
}

void ClockReplacer::Pin(frame_id_t frame_id) {
    const std::lock_guard<mutex_t> guard(mutex_);
    // Todo: you can implement it!

    circular_[frame_id] = Status::EMPTY_OR_PINNED;
}

void ClockReplacer::Unpin(frame_id_t frame_id) {
    const std::lock_guard<mutex_t> guard(mutex_);
    // Todo: you can implement it!
    circular_[frame_id] = Status::ACCESSED;
}

size_t ClockReplacer::Size() {
//<<<<<<< HEAD
    const std::lock_guard<mutex_t> guard(mutex_);
//=======
//>>>>>>> 73b7923430007cbb7b517b0632ad4e27598a31a0
    // Todo:
    // 返回在[arg0, arg1)范围内满足特定条件(arg2)的元素的数目
    // return all items that in the range[circular_.begin, circular_.end )
    // and be met the condition: status!=EMPTY_OR_PINNED
    // That is the number of frames in the buffer pool that storage page (NOT EMPTY_OR_PINNED)
    /*size_t size = capacity_;
    for( int i = 0; i < capacity_; i++ )
        if( circular_[i] == Status::EMPTY_OR_PINNED )
            size--;
    return size;*/
    size_t size = circular_.size();
    for( size_t i = 0; i < circular_.size(); i++ )
        if( circular_[i] == Status::EMPTY_OR_PINNED )
            size--;
    return size;
}
