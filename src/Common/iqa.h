#ifndef zlm_iqa_h
#define zlm_iqa_h
#include <functional>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/types_c.h>
#include "libcff/cff.h"
#include "Extension/Frame.h"
#include <memory>
#include <chrono>
//流质量评价
struct IQAResult {
    std::string start_time;
    std::string end_time;

    float loss_pkt_rate_total = 0;//总丢包率,越小越好
    float loss_pkt_rate_minu1 = 0;//1分钟丢包率,越小越好
    
    float block_detect = 0;
    float brightness_detect = 0;
    float snow_noise_detect = 0;
    float sharpness_detect = 0;

    // //基础参数
     float lum = 0;//平均亮度,0-1
     float crts = 0;//对比度0-1,越接近0.5越好

    // float chroma = 0.0;//色度,饱和度
    // //扩展参数
    // float noise = 0;//噪声,越小越好
    // float acuity = 0;//锐度,细节展现度

    
    // float sharpness = 0;//清晰度
    // void dump() {
    //     printf("result:\n"
    //         "\tblock\t%.3f\n"
    //         "\tluminance\t%.3f\n"
    //         "\tnoise\t%.3f\n"
    //         "\tsharpness\t%.3f\n",
    //         block,
    //         lum,
    //         noise,
    //         sharpness
    //     );
    // }
};
using iqa_cb_t = std::function<void(const IQAResult &result, int32_t err, const std::string msg)>;

class IQA
{
public:
    // //支持的算法
    // enum class algorithm_t{
    //     luminance = 0x01,
    //     contrastive = 0x02,
    //     noise = 0x04,
    //     block_artifact = 0x08,
    //     all = 0xffff,
    // };
    IQA(iqa_cb_t cb):iqa_cb(cb){};
    ~IQA(){
        uninit();
    }
    int32_t init(AVCodecID codecID, std::string& msg){
        begin_time = std::chrono::system_clock::now();
        if (codecID == AVCodecID::AV_CODEC_ID_NONE) {
            msg = "无法获取视频编码";
            return -1;
        }
        //初始化解码器和解析器
        decoder_ptr = std::make_shared<cff::av_decoder_context_t>(codecID);
        assert(*(decoder_ptr));
        if(!(*(decoder_ptr))){
            msg = "创建解码器失败";
            goto err;
        }
        packet_parser_ptr = std::make_shared<cff::avcodec_parser_t>(codecID);
        if (packet_parser_ptr == nullptr) {
            msg = "创建packet-parser失败";
            goto err;
        }
        return 0;
        err:
        uninit();
        return -1;
    }
    void uninit(){
        iqa_cb = nullptr;
        decoder_ptr.reset();
        packet_parser_ptr.reset();
    }
    
    int32_t push_frame(const mediakit::Frame::Ptr& frame, IQAResult &result, std::string &msg){
        auto now = std::chrono::system_clock::now();
        auto long_time = std::chrono::duration_cast<std::chrono::milliseconds>(begin_time - now).count();
        if(long_time > 60 * 1000){
            msg = "任务超时已停止";
            return -1;
        }
        if (frame->getTrackType() != mediakit::TrackType::TrackVideo) {
            msg = "内部错误, 无效的frame";
            return -1;    
        }
        cff::avpacket_t pkt;
        auto data = reinterpret_cast<uint8_t*>(frame->data());
        uint32_t size = frame->size();
        if(packet_parser_ptr->parse(*decoder_ptr, data, size, pkt) != 0){
            msg = "packet解析失败";
            return -1;
        }
        if(pkt.size() == 0){
            //数据不足
            return 1;
        }
        decoder_ptr->send_packet(pkt);
        cff::avframe_t avframe;
        int ret = decoder_ptr->recv_frame(avframe);
        if(ret == AVERROR(EAGAIN)){
            return 1;
        }
        if(ret < 0){
            msg = "解码失败";
            return ret;
        }
       

        return parse_frame(avframe, result);
}
    int32_t parse_frame(cff::avframe_t& frame, IQAResult& result) {
        auto begin = std::chrono::system_clock::now();
        //rgb mat
        cv::Mat src_mat = avframeToRGBmat(frame.native());
        //亮度&对比度
        luminance_contrast_detect(frame, result.lum, result.crts);
        cv::Mat gray_mat;
        cv::cvtColor(src_mat, gray_mat, CV_RGB2GRAY);
        //块
        result.block_detect = block_detect(gray_mat);
        result.brightness_detect = brightnessDetect(src_mat);
        result.snow_noise_detect = snowNoiseDetect(gray_mat);
        result.sharpness_detect = sharpnessDetect(gray_mat);
        auto end = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        return 0;
    }
    cv::Mat avframeToRGBmat(const AVFrame* frame)
    {
        int width = frame->width;
        int height = frame->height;
        cv::Mat image(height, width, CV_8UC3);
        int cvLinesizes[1];
        cvLinesizes[0] = image.step1();
        SwsContext* conversion = sws_getContext(width, height, (AVPixelFormat)frame->format, width, height, AVPixelFormat::AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data, cvLinesizes);
        sws_freeContext(conversion);
        return image;
    }

    //分析亮度和对比度
    static int32_t luminance_contrast_detect(const cff::avframe_t& yuv_frame, float& luminous, float& contrast) {
        const AVFrame* p = yuv_frame.native();
        float sum = 0;
        uint32_t max_lum = 0, min_lum = 255;
        for (int h = 0; h < p->height; h++) {
            for (int w = 0; w < p->width; w++) {
                uint8_t y = p->data[0][h * p->linesize[0] + w];
                sum += y;
                if (y > max_lum) {
                    max_lum = y;
                }
                if (y < min_lum) {
                    min_lum = y;
                }
            }
        }
        luminous = sum / (p->width * p->height);
        if(max_lum == 0 && min_lum == 0){
            contrast = 0;
        }else{
            contrast = (max_lum - min_lum) / (float)(max_lum + min_lum);
        }
        return 0;
    }
    /**
     * @brief 计算坏块率
     * 
     * @param gray 灰度图像
     * @param nb_zero 在bsize*bsize的区域内, 如果梯度不为0的个数小于nozero, 就认为是块
     * @param bsize 
     * @return float 0.0-1.0
     */
    float block_detect(const cv::Mat& gray, int nb_zero = 2, int32_t bsize = 8) {
        float block_num = 0, normal_num = 0;
        cv::Mat sobel;
        cv::Sobel(gray, sobel, CV_32FC1, 1, 1, 3);

        cv::Rect rt(0,0,bsize,bsize);    
        for(int j = 0; j < sobel.rows / bsize; j++) {
            rt.x = 0;
            for(int i = 0; i < sobel.cols / bsize; i++) {
                cv::Mat sub_mat = sobel(rt).clone();
                float solel_sum = cv::countNonZero(sub_mat);
                if(solel_sum >= nb_zero){
                    normal_num ++;
                }else{
                    block_num ++;
                }
                rt.x += bsize;
            }
            rt.y += bsize;
        }
        if(normal_num == 0 && block_num == 0){
            return 0.0;
        }
        //如果多个分区的非0值相同, 也认为是色块?
        return block_num / (normal_num + block_num);
    }
    //块效应
    static double blockDetect(const cv::Mat& srcImg)
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
    double brightnessDetect(const cv::Mat& srcImg)
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
    double snowNoiseDetect(const cv::Mat& gray_mat) 
    {
        assert(gray_mat.channels() == 1);

        cv::Mat img_m;

        int width = gray_mat.cols;
        int height = gray_mat.rows;

        // 对图像进行均值滤波去噪
        //  cvSmo oth(img,img_m,CV_MEDIAN,3,img->nChannels);
        cv::Mat kernel_gauss = (cv::Mat_<double>(3, 3) << 1, 2, 1,
            2, 4, 2,
            1, 2, 1);
        kernel_gauss /= 16;

        filter2D(gray_mat, img_m, gray_mat.depth(), kernel_gauss, cv::Point(-1, -1));

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
                gray_b = gray_mat.at<uint8_t>(r, c);
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
    double sharpnessDetect(const cv::Mat& gray_mat)
    {
        // 高斯模糊区域大小
        int gaussianSize = 3;
        assert(gray_mat.channels() == 1);
        cv::Mat out;

        // 进行高斯处理，处理的是指针img指向的内存，将处理后的数据交给out指针指向的内存，对每个像素周围gaussianSize*gaussianSize的区域进行高斯平滑处理（其实输入输出图像可以是相同的）
        cv::GaussianBlur(gray_mat, out, cv::Size(gaussianSize, gaussianSize), 0, 0);

        unsigned long d_Fver = 0, d_Fhor = 0, d_Bver = 0, d_Bhor = 0; // 水平统计,垂直统计,F是原图,B是模糊图
        unsigned long vver = 0, vhor = 0;
        unsigned long s_Fver = 0, s_Fhor = 0, s_Vver = 0, s_Vhor = 0;
        double b_Fver = 0.0, b_Fhor = 0.0;
        double blur_F = 0.0;

        for (int r = 0; r < gray_mat.rows; r++)
        {
            for (int c = 0; c < gray_mat.cols; c++)
            {
                // 垂直统计
                if (r != 0)                                                      // 如果不是最上侧的点
                    d_Fver = abs(gray_mat.at<uchar>(r, c) - gray_mat.at<uchar>(r - 1, c)); // 与上侧点的像素值相减
                // 水平统计
                if (c != 0) // 如果不是最左侧的点，
                    d_Fhor = abs(gray_mat.at<uchar>(r, c) - gray_mat.at<uchar>(r, c - 1));

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
    private:
    std::chrono::system_clock::time_point begin_time;
    iqa_cb_t iqa_cb;
    cff::av_decoder_context_ptr_t decoder_ptr;
    cff::avcodec_parser_ptr_t packet_parser_ptr;
};
#endif