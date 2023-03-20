#ifndef cff_avpacket_h
#define cff_avpacket_h
#include "cff_base.h"
extern "C" {
#include "libavcodec/avcodec.h"
}
namespace cff{
    struct avpacket_allocator_t {
        AVPacket* operator()()const {
            return av_packet_alloc();
        }
    };
    struct avpacket_deleter_t {
        void operator()(AVPacket* ptr)const {
            av_packet_free(&ptr);
        }
    };
    class avcodec_parser_t;
    class avpacket_t :public enable_ff_native<AVPacket, avpacket_allocator_t, avpacket_deleter_t>{
        public:
        avpacket_t() = default;
        avpacket_t(avpacket_t&&other) = default;
        avpacket_t& operator = (avpacket_t&& other) = default;
        uint8_t * data(){
            return native()->data;
        }
        uint32_t size(){
            return native()->size;
        }
        int32_t stream_index(){
            return native()->stream_index;
        }
        int64_t pts(){
            return native()->pts;
        }
        int64_t dts(){
            return native()->dts;
        }
        private:
        friend class avcodec_parser_t;
        uint8_t ** data_ptr(){
            return &native()->data;
        }
        int32_t * size_ptr(){
            return &native()->size;
        }
    };
    using avpacket_ptr_t = std::shared_ptr<avpacket_t>;
}
#endif