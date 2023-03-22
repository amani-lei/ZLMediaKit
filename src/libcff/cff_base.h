#ifndef cff_base_h
#define cff_base_h

#include <memory>

extern "C" {
#include "libavutil/avutil.h"
#include "libavutil/pixfmt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}


namespace cff {

    /**
     * @brief 一个通用的native封装, 使用智能指针维护native指针
     *
     * @tparam T native实际类型
     * @tparam Alloc 分配器
     * @tparam Del 释放器
     */
    template<typename T, typename Alloc, typename Del>
    struct enable_ff_native {
    public:
        enable_ff_native(enable_ff_native&& other) {
            native_ptr = std::move(other.native_ptr);
        }
        enable_ff_native& operator = (enable_ff_native&& other) = default;

        template<typename ... Args>
        enable_ff_native(Args ... args):native_ptr(Alloc()(args...), Del()) {

        }
        T* native() {
            return native_ptr.get();
        }
        const T* native()const {
            return native_ptr.get();
        }
        /**
         * @brief 放弃native指针, 放弃后native内存由上层自行管理
         *
         */
        T* release() {
            return native_ptr.release();
        }
        operator bool() {
            return native_ptr.get() != nullptr;
        }
        bool operator == (const enable_ff_native& other) {
            return native_ptr == other.native_ptr;
        }
        bool operator == (std::nullptr_t null) {
            return !native_ptr;
        }
    protected:
        void set_native(T* ptr)noexcept {
            native_ptr = { ptr, Del() };
        }
    private:
        std::unique_ptr<T, Del> native_ptr;
    };
}
#endif