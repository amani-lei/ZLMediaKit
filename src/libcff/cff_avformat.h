#ifndef cff_avformat_h
#define cff_avformat_h
#include <memory>
#include "cff_base.h"
#include "cff_avpacket.h"
namespace cff{
    struct avformat_context_allocator_t{
        AVFormatContext* operator()()const{
            return nullptr;//avformat_alloc_context();
        }
    };
    struct avformat_context_deleter_t{
        void operator()(AVFormatContext* ptr)const{
            avformat_free_context(ptr);
        }
    };

    class avformat_context_t:public enable_ff_native<AVFormatContext, avformat_context_allocator_t, avformat_context_deleter_t> {
        public:
        avformat_context_t() = default;
        avformat_context_t(avformat_context_t&&other) = default;
        avformat_context_t& operator = (avformat_context_t&&other) = default;
        
        AVStream * stream(int32_t idx){
            return native()->streams[idx];
        }
        AVStream * video_stream(){
            return find_stream(AVMEDIA_TYPE_VIDEO);
        }
        AVStream * audio_stream(){
            return find_stream(AVMEDIA_TYPE_AUDIO);
        }
        AVStream * find_stream(AVMediaType type){
            auto n = native()->nb_streams;
            for(uint32_t i=0; i < n; i++) {
                auto s = native()->streams[i];
                if(s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
                    return s;
                }
            }
            return nullptr;
        }
        int32_t open_input(const char * url, AVDictionary ** opts = nullptr){
            assert(native() == nullptr);
            set_native(avformat_alloc_context());
            auto p = native();
            int32_t ret = avformat_open_input(&p, url, nullptr, opts);
            if(ret < 0){
                release();
            }
            ret = avformat_find_stream_info(p, nullptr);
            return ret;
        }
        int32_t open_output(const char * url){
            assert(native() == nullptr);
            AVFormatContext *p = nullptr;
            int ret = avformat_alloc_output_context2(&p, nullptr, nullptr, url);
            if(ret < 0){
                return ret;
            }

            if(p->flags & AVFMT_NOFILE){
                ret = avio_open2(&p->pb, url, 0, 0, 0);
                if(ret < 0){
                    return ret;
                }
            }
            set_native(p);
            return 0;
        }
        int32_t read_packet(avpacket_t & pkt){
            int32_t ret = av_read_frame(native(), pkt.native());
            return ret;
        }

        int32_t write_header(){
            int32_t ret = avformat_write_header(native(), nullptr);
            return ret;
        }

        int32_t write_packet(avpacket_t & pkt){
            int32_t ret = av_write_frame(native(), pkt.native());
            return ret;
        }

        int32_t write_trailer(){
            int32_t ret = av_write_trailer(native());
            ret |= avio_close(native()->pb);
            return ret;
        }
        int32_t close(){
            if(is_input()){
                return close_input();
            }
            if(is_output()){
                return close_output();
            }
            assert(false);
            return -1;
        }
        int32_t is_input(){
            return native()->iformat ? 1 : 0;
        }
        int32_t is_output(){
            return native()->oformat ? 1 : 0;
        }
        protected:
        int32_t close_input(){
            assert(native()->iformat);
            if(native()->iformat){
                return -1;
            }
            auto p = native();
            this->release();
            avformat_close_input(&p);
            return 0;
        }
        int32_t close_output(){
            assert(native()->oformat);
            if(native()->oformat){
                return -1;
            }
            if(native()->oformat->flags & AVFMT_NOFILE){
                int32_t ret = avio_closep(&native()->pb);
                return ret;
            }
            return 0;
        }
        
    };
    using avformat_context_ptr_t = std::shared_ptr<avformat_context_t>;

}
#endif