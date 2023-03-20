#ifndef cff_avfilter_context_h
#define cff_avfilter_context_h
#include "cff_avfilter.h"
namespace cff{
    //avfiltercontex
    struct avfilter_context_allocator_t {
        AVFilterContext* operator()(AVFilterContext * ctx)const {
            return ctx;
        }
    };

    struct avfilter_context_deleter_t {
        void operator()(AVFilterContext* ptr)const {
            avfilter_free(ptr);
        }
    };

    class avfilter_context_t : public enable_ff_native<AVFilterContext, avfilter_context_allocator_t, avfilter_context_deleter_t>{
        public:
        using base_class_t = enable_ff_native<AVFilterContext, avfilter_context_allocator_t, avfilter_context_deleter_t>;
        avfilter_context_t(avfilter_context_t&&other) = default;
        avfilter_context_t& operator = (avfilter_context_t&&other) = default;
        avfilter_context_t(AVFilterContext * ctx):base_class_t(ctx){

        }
        int32_t send_frame(avframe_t& frame){
            return av_buffersrc_add_frame(native(), frame.native());
        }
        int32_t recv_frame(avframe_t & frame){
            int ret = av_buffersink_get_frame(native(), frame.native());
            const auto x = av_err2str(ret);
            return ret;
        }
    };
    using avfilter_context_ptr_t = std::shared_ptr<avfilter_context_t>;

}
#endif