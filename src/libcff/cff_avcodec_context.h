#ifndef cff_avcodec_h
#define cff_avcodec_h
#include "cff_base.h"
#include "cff_avpacket.h"
#include "cff_avframe.h"
#include <type_traits>
namespace cff {
    template<bool IsDec>
    struct avcodec_context_base_allocator_t {
        typename std::enable_if<IsDec, AVCodecContext*>::type operator()(AVStream* stream)const {
            static_assert(IsDec, "only decoder...");
            auto c = avcodec_find_decoder(stream->codecpar->codec_id);
            auto p = avcodec_alloc_context3(c);
            if(p == nullptr){
                return nullptr;
            }
            int ret = avcodec_parameters_to_context(p, stream->codecpar);
            ret |= avcodec_open2(p, c, nullptr);
            return ret == 0 ? p : nullptr;
        }
        AVCodecContext* operator()(AVCodecID codec)const {
            auto c = avcodec_find_decoder(codec);
            auto p = avcodec_alloc_context3(c);
            if(p == nullptr){
                return nullptr;
            }
            if(IsDec){
                avcodec_open2(p, c, nullptr);
            }
            return p;
        }
        AVCodecContext* operator()(AVCodec* codec)const {
            auto p = avcodec_alloc_context3(codec);
            if(p == nullptr){
                return nullptr;
            }
            if(IsDec){
                avcodec_open2(p, codec, nullptr);
            }
            return p;
        }
        AVCodecContext* operator()(const char* codec)const {
            auto c = IsDec ? avcodec_find_decoder_by_name(codec) : avcodec_find_encoder_by_name(codec);
            if(c == nullptr){
                return nullptr;
            }
            auto p = avcodec_alloc_context3(c);
            if(p == nullptr){
                return nullptr;
            }
            if (IsDec){
                int ret = avcodec_open2(p, c, nullptr);
            }
            return p;
        }
    };

    struct avcodec_context_deleter_t {
        void operator()(AVCodecContext* ptr)const {
            avcodec_free_context(&ptr);
        }
    };
   
    template<bool IsDec>
    class avcodec_context_base_t:public enable_ff_native<AVCodecContext, avcodec_context_base_allocator_t<IsDec>, avcodec_context_deleter_t>{
        using base_class_t = enable_ff_native<AVCodecContext, avcodec_context_base_allocator_t<IsDec>, avcodec_context_deleter_t>;
        public:
        avcodec_context_base_t(avcodec_context_base_t&&other) = default;
        avcodec_context_base_t& operator = (avcodec_context_base_t&&other) = default;
        avcodec_context_base_t(AVStream * stream):base_class_t(stream){
            
        }
        avcodec_context_base_t(AVCodec * codec):base_class_t(codec){
            
        }
        avcodec_context_base_t(AVCodecID codec):base_class_t(codec){
            
        }
        avcodec_context_base_t(const char * codec):base_class_t(codec){
            
        }
        int32_t open(){
            int32_t ret = avcodec_open2(base_class_t::native(), nullptr, nullptr);
            return ret;
        }
        int32_t send_packet(avpacket_t & pkt){
            int ret = avcodec_send_packet(base_class_t::native(), pkt.native());
            const auto x = av_err2str(ret);
            return ret;
        }
        int32_t recv_frame(avframe_t & frame){
            int ret = avcodec_receive_frame(base_class_t::native(), frame.native());
            const auto x = av_err2str(ret);
            return ret;
        }
    };
    using av_decoder_context_t = avcodec_context_base_t<true>;
    using av_decoder_context_ptr_t = std::shared_ptr<av_decoder_context_t>;

    using av_encoder_context_t = avcodec_context_base_t<false>;
    using av_encoder_context_ptr_t = std::shared_ptr<av_encoder_context_t>;
}
#endif