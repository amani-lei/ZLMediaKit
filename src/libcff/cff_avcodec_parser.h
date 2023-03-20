#ifndef cff_avcodec_parser_t
#define cff_avcodec_parser_t
#include "cff_base.h"
#include "cff_avcodec_context.h"
namespace cff{
    struct avcodec_parser_allocator_t{
        AVCodecParserContext* operator()(AVCodecID codec)const{
            return av_parser_init(codec);
        }
        AVCodecParserContext* operator()(const char * codec)const{
            const AVCodecDescriptor * c = avcodec_descriptor_get_by_name(codec);
            if(c == nullptr)return nullptr;
            return av_parser_init(c->id);
        }
    };
    
    struct avcodec_parser_deleter_t{
        void operator()(AVCodecParserContext* ptr)const {
            av_parser_close(ptr);
        }
    };

    class avcodec_parser_t:public enable_ff_native<AVCodecParserContext, avcodec_parser_allocator_t, avcodec_parser_deleter_t>{
        public:
        using base_class_t = enable_ff_native<AVCodecParserContext, avcodec_parser_allocator_t, avcodec_parser_deleter_t>;
        avcodec_parser_t(avcodec_parser_t&&other) = default;
        avcodec_parser_t& operator = (avcodec_parser_t&&other) = default;

        avcodec_parser_t(AVCodecID codec):base_class_t(codec){}

        int32_t parse(av_decoder_context_t & codec, uint8_t * data, uint32_t size, avpacket_t & packet){
            avpacket_ptr_t p = std::make_shared<avpacket_t>();
            int32_t ret = av_parser_parse2(native(), codec.native(), packet.data_ptr(), packet.size_ptr(), 
            data, size,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            return ret;
        }
    };
    using avcodec_parser_ptr_t = std::shared_ptr<avcodec_parser_t>;
} 
#endif