#ifndef cff_swscale_h
#define cff_swscale_h
#include "cff_base.h"
namespace cff{
    struct swscale_allocator_t {
        SwsContext* operator()(int32_t src_width, int32_t src_height, AVPixelFormat src_fmt, 
        int32_t dst_width, int32_t dst_height, AVPixelFormat dst_fmt)const {
            return sws_getContext(src_width, src_height, src_fmt, dst_width, dst_height, dst_fmt, 0, 0, 0);
        }
    };
    struct swscale_deleter_t {
        void operator()(SwsContext* ptr)const {
            sws_freeContext(ptr);
        }
    };
    class swscale_t :public enable_ff_native<SwsContext, swscale_allocator_t, swscale_deleter_t>{
        public:
        using base_class_t = enable_ff_native<SwsContext, swscale_allocator_t, swscale_deleter_t>;
        swscale_t() = default;
        swscale_t(swscale_t&&other) = default;
        swscale_t& operator = (swscale_t&& other) = default;

        swscale_t(int32_t src_width, int32_t src_height, AVPixelFormat src_fmt, 
        int32_t dst_width, int32_t dst_height, AVPixelFormat dst_fmt)
        :base_class_t(src_width, src_height, src_fmt, dst_width, dst_height, dst_fmt) {}

        int32_t scale_frame(const avframe_t & in, avframe_t & out){
            int ret = sws_scale(native(), in.data(), in.linesize(), 0, in.height(), out.data(), out.linesize());
            return ret;
        }
    };
    using swscale_ptr_t = std::shared_ptr<swscale_t>;
}
#endif