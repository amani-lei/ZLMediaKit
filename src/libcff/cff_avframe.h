#ifndef cff_avframe_h
#define cff_avframe_h

#include "cff_base.h"
extern "C" {
#include "libavcodec/avcodec.h"
}
namespace cff{
    struct avframe_deleter_t {
        void operator()(AVFrame* ptr)const {
            av_frame_free(&ptr);
        }
    };
    struct avframe_allocator_t{
        AVFrame* operator()()const {
            return av_frame_alloc();
        }
    };
    class avframe_t :public enable_ff_native<AVFrame, avframe_allocator_t, avframe_deleter_t>{
        public:
        avframe_t(avframe_t&&other) = default;
        avframe_t& operator = (avframe_t&&other) = default;
        avframe_t() = default;
        uint8_t ** data(){
            return &native()->data[0];
        }
        int32_t* linesize() {
            return &native()->linesize[0];
        }
        void set_width(int32_t width){
            native()->width = width;
        }
        void set_height(int32_t height){
            native()->height = height;
        }
        int32_t width()const {
            return native()->width;
        }
        int32_t height() const {
            return native()->height;
        }
        int32_t samples() const {
            return native()->nb_samples;
        }
        AVPixelFormat format() const {
            return static_cast<AVPixelFormat>(native()->format);
        }
        int32_t alloc_buffer(int32_t align){
            int32_t ret = av_frame_get_buffer(native(),align);
            return ret;
        }
        int32_t alloc_buffer(AVPixelFormat format, int32_t align = 0){
            native()->format = format;
            return alloc_buffer(align);
        }
        int32_t alloc_buffer(int32_t width, int32_t height, AVPixelFormat format, int32_t align = 0){
            native()->width = width;
            native()->height = height;
            native()->format = format;
            return alloc_buffer(align);
        }
        int32_t alloc_buffer(AVSampleFormat format, int32_t align = 0){
            native()->format = format;
            return alloc_buffer(align);
        }
        int32_t alloc_buffer(int32_t nb_sample, AVSampleFormat format, int32_t align = 0){
            native()->nb_samples = nb_sample;
            native()->format = format;
            return alloc_buffer(align);
        }
    };
    using avframe_ptr_t = std::shared_ptr<avframe_t>;
}
#endif