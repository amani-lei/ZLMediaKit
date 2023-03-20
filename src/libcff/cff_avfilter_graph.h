#ifndef cff_avfilter_graph_h
#define cff_avfilter_graph_h
#include "cff_avfilter_context.h"
#include "cff_avfilter_inout.h"
namespace cff{

    struct avfilter_graph_allocator_t {
        AVFilterGraph* operator()()const {
            return avfilter_graph_alloc();
        }
    };
    struct avfilter_graph_deleter_t {
        void operator()(AVFilterGraph* ptr)const {
            avfilter_graph_free(&ptr);
        }
    };

    class avfilter_graph_t:public enable_ff_native<AVFilterGraph, avfilter_graph_allocator_t, avfilter_graph_deleter_t>{
        public:
        avfilter_graph_t() = default;
        avfilter_graph_t(avfilter_graph_t&&other) = default;
        avfilter_graph_t& operator = (avfilter_graph_t&&other) = default;
        avfilter_context_t create_filter_context(avfilter_t& filter, const char * name, const char * args){
            AVFilterContext * ctx = nullptr;
            int ret = avfilter_graph_create_filter(&ctx, filter.native(), name, args, nullptr, native());
            return {ctx};
        }
        /**
         * @brief 配置到filter_graph
         * 
         * @param filters 
         * @param input 无论成功还是失败, 上层不应该继续持有该input
         * @param output 无论成功还是失败, 上层不应该继续持有该output
         * @return int32_t 
         */
        int32_t config(const char * filters, avfilter_inout_t& input, avfilter_inout_t& output){
            auto in = input.native();
            auto out = output.native();
            int32_t ret = avfilter_graph_parse_ptr(native(), filters, &in, &out, NULL);
            if(ret < 0){
                return ret;
            }
            //如果被ffmpeg释放, 应该放弃对底层指针的管理
            if(in == nullptr){
                input.release();
            }
            if(out == nullptr){
                output.release();
            }
            ret = avfilter_graph_config(native(), nullptr);
            return ret;
        }
    };
    using avfilter_graph_ptr_t = std::shared_ptr<avfilter_graph_t>;
}
#endif