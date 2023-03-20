#ifndef cff_avfilter_inout_h
#define cff_avfilter_inout_h
#include "cff_avfilter_context.h"
namespace cff{
    struct avfilter_inout_allocator_t {
        AVFilterInOut* operator()()const {
            return avfilter_inout_alloc();
        }
    };

    struct avfilter_inout_deleter_t {
        void operator()(AVFilterInOut* ptr)const {
            avfilter_inout_free(&ptr);
        }
    };

    class avfilter_inout_t: public enable_ff_native<AVFilterInOut, avfilter_inout_allocator_t, avfilter_inout_deleter_t>{
        public:
        avfilter_inout_t(avfilter_inout_t&&other) = default;
        avfilter_inout_t& operator = (avfilter_inout_t&&other) = default;
        avfilter_inout_t(const char * name, avfilter_context_t& filter_ctx){
            auto p = native();
            p->name = av_strdup(name);
            p->filter_ctx = filter_ctx.native();
            p->pad_idx = 0;
            p->next = nullptr;
        }
    };
}
#endif