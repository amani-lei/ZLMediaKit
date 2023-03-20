#ifndef cff_gauss_filter_t
#define cff_gauss_filter_t
#include "cff_avfilter_graph.h"
class gauss_filter_t{
    public:
        gauss_filter_t():buffersrc_ctx(nullptr), buffersink_ctx(nullptr){
            //init();
        }
        
        int32_t init(int32_t width, int32_t height, AVPixelFormat pixfmt){
            using namespace cff;
            init_input("in", width, height, pixfmt);
            init_output("out", width, height, pixfmt);
            assert(buffersrc_ctx && buffersink_ctx);
            if(!(buffersrc_ctx && buffersink_ctx)){
                return -1;
            }
            avfilter_inout_t input("out", buffersink_ctx);
            avfilter_inout_t output("in", buffersrc_ctx);
            assert(input && output);
            if(!(input && output)){
                return -1;
            }
            if(filter_graph.config("gblur", input, output) != 0){
                return -1;
            }
            return 0;
        }

        int32_t init_input(const char *name, int32_t width, int32_t height, AVPixelFormat pix_fmt){
            char args[512];
            snprintf(args,512, "video_size=%dx%d:pix_fmt=%s:time_base=1/25", width, height, av_get_pix_fmt_name(pix_fmt));

            cff::avfilter_t fbuf("buffer");
            buffersrc_ctx = filter_graph.create_filter_context(fbuf, name, args);
            if(!buffersrc_ctx){
                return -1;
            }
            return 0;
        }
        int32_t init_output(const char *name, int32_t width, int32_t height, AVPixelFormat pix_fmt){
            char args[512];
            snprintf(args,512, "video_size=%dx%d:pix_fmt=%s:time_base=1/25", width, height, av_get_pix_fmt_name(pix_fmt));

            cff::avfilter_t fbufsink("buffersink");
            buffersink_ctx = filter_graph.create_filter_context(fbufsink, name,nullptr);
            if(!buffersink_ctx){
                return -1;
            }
            return 0;
        }

        int32_t send_frame(cff::avframe_t& frame){
            return buffersrc_ctx.send_frame(frame);
        }
        int32_t recv_frame(cff::avframe_t & frame){
            return buffersink_ctx.recv_frame(frame);
        }
        private:
        cff::avfilter_graph_t filter_graph;
        cff::avfilter_context_t buffersrc_ctx;
        cff::avfilter_context_t buffersink_ctx;
    };
#endif