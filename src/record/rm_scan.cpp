#include "rm_scan.h"

#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 *
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    // Todo:
    // 初始化file_handle和rid（指向第一个存放了记录的位置）
    if( file_handle->file_hdr_.num_pages == 1 ) {
        rid_.slot_no = file_handle->file_hdr_.num_records_per_page;
        rid_.page_no = 0;
    }
    else {
        rid_.slot_no = Bitmap::first_bit( 1, file_handle->fetch_page_handle(1).bitmap, file_handle->file_hdr_.num_records_per_page );
        rid_.page_no = 1;
    }
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // Todo:
    // 找到文件中下一个存放了记录的非空闲位置，用rid_来指向这个位置
    RmPageHandle page_handle = file_handle_->fetch_page_handle( rid_.page_no );
    int records_per_page =  file_handle_->file_hdr_.num_records_per_page;
    rid_.slot_no = Bitmap::next_bit( 1, page_handle.bitmap, records_per_page, rid_.slot_no );
    int pages_max = file_handle_->file_hdr_.num_pages;

    while( rid_.slot_no == records_per_page && rid_.page_no < pages_max ) { // if slot_no==records_per_page means cur_page is full
        rid_.page_no++;
        page_handle = file_handle_->fetch_page_handle( rid_.page_no );
        rid_.slot_no = Bitmap::first_bit( 1, page_handle.bitmap, records_per_page );
    }

}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // Todo: 修改返回值
    return rid_.slot_no == file_handle_->file_hdr_.num_records_per_page;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    // Todo: 修改返回值
    return rid_;
}
