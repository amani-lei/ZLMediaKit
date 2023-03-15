#ifndef zlm_qs_h
#define zlm_qs_h
#include <functional>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/types_c.h>
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavfilter/avfilter.h"
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include "extension/Frame.h"
#include <memory>
//流质量评价
struct StreamAppraiseResult{
    //float pkt_loss_rote = 0;//丢包率,越小越好
    float contrastive = 0;//对比度0-1,越接近0越好
    float luminance = 0;//亮度0-1,越接近0越好
    float noise = 0;//噪声,越小越好
    float block_artifact = 0;//块效应,越小越好
};
struct AVPacketDeleter{
    void operator()(AVPacket* ptr)const{
        av_packet_free(&ptr);
    }
};
struct AVFrameDeleter{
    void operator()(AVFrame* ptr)const{
        av_frame_free(&ptr);
    }
};
struct AVCodecContextDeleter{
    void operator()(AVCodecContext* ptr)const{
        avcodec_free_context(&ptr);
    }
};
struct AVCodecParserContextDeleter{
    void operator()(AVCodecParserContext* ptr)const{
        avcodec_parameters_free(&ptr);
    }
};
struct AVFilterInOutDeleter{
    void operator()(AVFilterInOut* ptr)const{
        avfilter_inout_free(&ptr);
    }
};
struct AVFilterGraphDeleter{
    void operator()(AVFilterGraph* ptr)const{
        avfilter_graph_free(&ptr);
    }
};

struct AVFilterContextDeleter{
    void operator()(AVFilterContext* ptr)const{
        avfilter_free(ptr);
    }
};
using FilterInOutPtr = std::shared_ptr<AVFilterInOut>;
using FilterGraphPtr = std::shared_ptr<AVFilterGraph>;
using FilterContextPtr =  std::shared_ptr<AVFilterContext>;
struct FilterContext{
    FilterGraphPtr filter;
    FilterContextPtr buffersrc_ctx;
    FilterContextPtr buffersink_ctx;
};
class StreamAppraise
{
    public:
    //支持的算法
    enum class algorithm_t{
        luminance = 0x01,
        contrastive = 0x02,
        noise = 0x04,
        block_artifact = 0x08,
        all = 0xffff,
    };
    StreamAppraise(mediakit::CodecId codecid){
        this->codecid = codecid;
        init_decoder();
    };
    virtual ~StreamAppraise() = default;
    using result_cb_t = std::function<void(const StreamAppraiseResult&)>;
    void on_result(result_cb_t cb){
        result_cb = cb;
    }
    void push_frame(mediakit::Frame::Ptr frame){
        if(decoder_ptr == nullptr){
            return;
        }
        int ret = av_parser_parse2(parser_ptr.get(), decoder_ptr.get(),
        &packet_ptr->data, &packet_ptr->size, 
        (uint8_t*)frame->data(), frame->size(), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if(ret < 0){
            return;
        }
        if(decode() != 0){
            return;
        }
        if(make_gray_frame() != 0){
            return;
        }


    }
    //块效应
    static double blockDetect(const cv::Mat &srcImg)
    {
        cv::Mat img, cont;
        //转换为灰度图像
        cv::cvtColor(srcImg, img, CV_RGB2GRAY);
        cv::GaussianBlur(img, img, cv::Size(3, 3), 0, 0);
        cv::Canny(img, cont, 0, 0);

        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;
        // contour
        cv::findContours(cont, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));
        int sum1 = hierarchy.size();
        cv::Canny(img, cont, 0, 15);
        cv::findContours(cont, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));
        int sum2 = hierarchy.size();
        return (double)1 - (double)sum2 / (sum1 == 0 ? 1 : sum1);
    }
    //过曝光
    double brightnessDetect(const cv::Mat &srcImg)
    {
        cv::Mat img;
        cv::cvtColor(srcImg, img, CV_BGR2Lab);

        int br;
        double bright = 0.0;

        auto width = srcImg.cols;
        auto height = srcImg.rows;
        for (int r = 0; r < height; r++)
        {
            for (int c = 0; c < width; c++)
            {
                br = img.at<cv::Vec3b>(r, c)[0];
                bright += br / 2.55 / width / height;
            }
        }
        return bright / 100;
    }
    //噪声/雪花屏
    double snowNoiseDetect(const cv::Mat &srcImg)
    {
        cv::Mat img;
        if (srcImg.empty())
            return -1;
        if (srcImg.channels() == 1) // 转换为单通道图
            img = srcImg;
        else
            cv::cvtColor(srcImg, img, cv::COLOR_BGR2GRAY);

        cv::Mat img_m;

        int width = img.cols;
        int height = img.rows;

        // 对图像进行均值滤波去噪
        //  cvSmo oth(img,img_m,CV_MEDIAN,3,img->nChannels);
        cv::Mat kernel_gauss = (cv::Mat_<double>(3, 3) << 1, 2, 1,
                                2, 4, 2,
                                1, 2, 1);
        kernel_gauss /= 16;

        filter2D(img, img_m, img.depth(), kernel_gauss, cv::Point(-1, -1));

        double psignal, pnoise; // 信号功率   噪音功率
        int gray_a, gray_b;
        double snr; // 信噪比

        psignal = 0;
        pnoise = 0;

        for (int r = 0; r < height; r++)
        {
            for (int c = 0; c < width; c++)
            {
                gray_a = img_m.at<uint8_t>(r, c);
                gray_b = img.at<uint8_t>(r, c);
                psignal += (double)gray_b * (double)gray_b / 255 / 255; // 这里是求噪音所占信号的比例
                pnoise += (double)(gray_b - gray_a) * (double)(gray_b - gray_a) / 255 / 255;
            }
        }
        if (psignal < 1)
            psignal = 1;
        if (pnoise < 1)
            pnoise = 1;
        snr = 20 * log(((double)psignal) / ((double)pnoise));

        return snr; // 返回信噪比，即图像质量
    }
    //清晰度
    double sharpnessDetect(const cv::Mat &srcImg)
    {
        // 高斯模糊区域大小
        int gaussianSize = 3;

        cv::Mat img;

        if (srcImg.channels() != 1) // 如果输入的图像不是单通道灰度图，则转换为灰度图
            cv::cvtColor(srcImg, img, cv::COLOR_BGR2GRAY);
        else
            img = srcImg;

        cv::Mat out;

        // 进行高斯处理，处理的是指针img指向的内存，将处理后的数据交给out指针指向的内存，对每个像素周围gaussianSize*gaussianSize的区域进行高斯平滑处理（其实输入输出图像可以是相同的）
        cv::GaussianBlur(img, out, cv::Size(gaussianSize, gaussianSize), 0, 0);

        unsigned long d_Fver = 0, d_Fhor = 0, d_Bver = 0, d_Bhor = 0; // 水平统计,垂直统计,F是原图,B是模糊图
        unsigned long vver = 0, vhor = 0;
        unsigned long s_Fver = 0, s_Fhor = 0, s_Vver = 0, s_Vhor = 0;
        double b_Fver = 0.0, b_Fhor = 0.0;
        double blur_F = 0.0;

        for (int r = 0; r < img.rows; r++)
        {
            for (int c = 0; c < img.cols; c++)
            {
                // 垂直统计
                if (r != 0)                                                      // 如果不是最上侧的点
                    d_Fver = abs(img.at<uchar>(r, c) - img.at<uchar>(r - 1, c)); // 与上侧点的像素值相减
                // 水平统计
                if (c != 0) // 如果不是最左侧的点，
                    d_Fhor = abs(img.at<uchar>(r, c) - img.at<uchar>(r, c - 1));

                // 垂直统计
                if (r != 0) // 如果不是最上侧的点
                    d_Bver = abs(out.at<uchar>(r, c) - out.at<uchar>(r - 1, c));
                // 水平统计
                if (c != 0) // 如果不是最左侧的点，
                    d_Bhor = abs(out.at<uchar>(r, c) - out.at<uchar>(r, c - 1));

                vver = (d_Fver - d_Bver > 0) ? (d_Fver - d_Bver) : 0;
                vhor = (d_Fhor - d_Bhor > 0) ? (d_Fhor - d_Bhor) : 0;

                s_Fver += d_Fver;
                s_Fhor += d_Fhor;
                s_Vver += vver;
                s_Vhor += vhor;
            }
        }
        b_Fver = (s_Fver - s_Vver) / ((double)s_Fver + 1);
        b_Fhor = (s_Fhor - s_Vhor) / ((double)s_Fhor + 1);
        blur_F = (b_Fver > b_Fhor) ? b_Fver : b_Fhor;

        return 1 - blur_F;
    }
    protected:
    int32_t init_decoder(){
        AVCodecID avc = AVCodecID::AV_CODEC_ID_NONE;
        if(this->codecid == mediakit::CodecH264){
            avc = AV_CODEC_ID_H264;
        }else if(this->codecid != mediakit::CodecH265){
            avc = AV_CODEC_ID_H265;
        }
        if(avc == AV_CODEC_ID_NONE){
            return -1;
        }
        const AVCodec* dec = avcodec_find_decoder(avc);
        if(dec == nullptr){
            return -1;
        }
        std::shared_ptr<AVCodecParserContext> parser = {av_parser_init(dec->id), AVCodecParserContextDeleter()};
        if(parser == nullptr){
            return -1;
        }
        std::shared_ptr<AVCodecContext> decoder = {avcodec_alloc_context3(dec), AVCodecContextDeleter()};
        if(decoder == nullptr){
            return -1;
        }
        if(avcodec_open2(decoder.get(), dec, nullptr) != 0){
            return -1;
        }

        this->parser_ptr = parser;
        this->decoder_ptr = decoder;
        this->packet_ptr = new_packet();;
        this->frame_ptr = new_frame();
        this->gray_frame_ptr = new_frame();
        return 0;
    }
    //初始化高斯滤波器
    int32_t init_gblur_filter(){
        std::shared_ptr<FilterContext> myfilter = std::make_shared<FilterContext>();
        const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        FilterInOutPtr inputs(avfilter_inout_alloc(), AVFilterInOutDeleter());
        FilterInOutPtr outputs(avfilter_inout_alloc(), AVFilterInOutDeleter());
        AVFilterContext * buffersrc_ctx = nullptr;
        AVFilterContext * buffersink_ctx = nullptr;
        myfilter->filter = {avfilter_graph_alloc(), AVFilterGraphDeleter()};
        int ret = 0;
        if (!outputs || !inputs || !myfilter->filter) {
            ret = AVERROR(ENOMEM);
            return ret;
        }
        {
            AVFilterContext *buffersrc_ctx = nullptr;
            AVFilterContext *buffersink_ctx = nullptr;

            avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", nullptr, nullptr, myfilter->filter.get());
            avfilter_graph_create_filter(&buffersink_ctx, buffersrc, "out", nullptr, nullptr, myfilter->filter.get());

            myfilter->buffersrc_ctx = {buffersrc_ctx, AVFilterContextDeleter()};
            myfilter->buffersink_ctx = {buffersink_ctx, AVFilterContextDeleter()};
            if(myfilter->buffersrc_ctx == nullptr || myfilter->buffersink_ctx == nullptr){
                return -1;
            }
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name       = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx;
        inputs->pad_idx    = 0;
        inputs->next       = NULL;
        auto input = inputs.get();
        auto output = outputs.get();
        if (avfilter_graph_parse_ptr(myfilter->filter.get(), "gblur", &input, &output, NULL) < 0){
            return -1;
        }

        if (avfilter_graph_config(myfilter->filter.get(), NULL) < 0){
            return -1;
        }
        gblur_filter_ptr = myfilter;
        return 0;
    }
    //生成灰度图
    int32_t make_gray_frame(){
        AVFrame * frame = frame_ptr.get();
        if(gray_sws_ptr == nullptr){
            std::shared_ptr<SwsContext> sws = {
                sws_getContext(frame->width, frame->height,(AVPixelFormat)frame->format,
                frame->width, frame->height, AV_PIX_FMT_GRAY8, 
                SWS_BILINEAR, 0, 0, 0),
            [](SwsContext * ptr){
                if(ptr){
                    sws_freeContext(ptr);
                }
            }};
            if(sws == nullptr){
                return -1;
            }
            gray_sws_ptr = sws;
        }
        return sws_scale_frame(gray_sws_ptr.get(), frame_ptr.get(), gray_frame_ptr.get());
    }

    int32_t make_gblur_frame(std::shared_ptr<AVFrame> inframe){
        if(gblur_filter_ptr == nullptr){
            return -1;
        }
        int ret = av_buffersrc_add_frame_flags(gblur_filter_ptr->buffersrc_ctx.get(), inframe.get(), AV_BUFFERSRC_FLAG_KEEP_REF);
        ret |= av_buffersink_get_frame(gblur_filter_ptr->buffersink_ctx.get(), gblur_frame_ptr.get());
        return ret;
    }

    //解码
    int32_t decode(){
        int ret = avcodec_send_packet(decoder_ptr.get(), packet_ptr.get());
        if(ret < 0){
            return ret;
        }
        ret = avcodec_receive_frame(decoder_ptr.get(), frame_ptr.get());
        if(ret != 0){
            return ret;
        }
        return 0;
    }
    
    std::shared_ptr<AVFrame> new_frame(){
        return{av_frame_alloc(), AVFrameDeleter()};
    }

    std::shared_ptr<AVPacket> new_packet(){
        return{av_packet_alloc(), AVPacketDeleter()};
    }
    
    private:
    result_cb_t result_cb;//分析结果回调
    mediakit::CodecId codecid;
    std::shared_ptr<AVCodecContext> decoder_ptr;//解码器
    std::shared_ptr<AVCodecParserContext> parser_ptr;//从数据中提取packet
    std::shared_ptr<AVPacket> packet_ptr;
    
    std::shared_ptr<AVFrame> frame_ptr;//原始图
    std::shared_ptr<AVFrame> gray_frame_ptr;//灰度图
    std::shared_ptr<AVFrame> gblur_frame_ptr;//高斯后的图

    std::shared_ptr<SwsContext> gray_sws_ptr;//灰度图的转换器
    std::shared_ptr<FilterContext> gblur_filter_ptr;//高斯滤波器
};
#endif