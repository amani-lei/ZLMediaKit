#ifndef cff_avfilter_h
#define cff_avfilter_h
#include "cff_base.h"
#include "cff_avframe.h"
namespace cff{
    struct avfilter_allocator_t{
        const AVFilter* operator()(const char*name)const{return avfilter_get_by_name(name);}
    };
    struct avfilter_deleter_t{
        void operator()(const AVFilter* ptr)const {}
    };
    class avfilter_t:public enable_ff_native<const AVFilter, avfilter_allocator_t, avfilter_deleter_t>{
        public:
        avfilter_t(avfilter_t&&other) = default;
        avfilter_t& operator = (avfilter_t&&other) = default;

        avfilter_t(const char * name):enable_ff_native<const AVFilter, avfilter_allocator_t, avfilter_deleter_t>(name){

        }
        operator bool(){
            return native() != nullptr;
        }
    };
}
#endif