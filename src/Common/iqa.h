#ifndef zlm_iqa_h
#define zlm_iqa_h
#include <functional>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/types_c.h>
#include "libcff/cff.h"
#include <memory>
#include <chrono>
//流质量评价
struct IQAResult{
    float pkt_loss_rote = 0;//丢包率,越小越好
    float contrastive = 0;//对比度0-1,越接近0越好
    float luminance = 0;//亮度0-1,越接近0越好
    float noise = 0;//噪声,越小越好
    float block_artifact = 0;//块效应,越小越好
    float sharpness = 0;//清晰度
    void dump(){
        printf("result:\n"
        "\tblock\t%.3f\n"
        "\tluminance\t%.3f\n"
        "\tnoise\t%.3f\n"
        "\tsharpness\t%.3f\n",
        block_artifact,
        luminance,
        noise,
        sharpness
        );
    }
};

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
    IQA() = default;
    virtual ~IQA() = default;
    using result_cb_t = std::function<void(const IQAResult&)>;
    void on_result(result_cb_t cb){
        result_cb = cb;
    }
    void push_frame(cff::avframe_t& frame){
        cv::Mat mat = avframeToCvmat(frame.native());
        IQAResult result;
        auto begin = std::chrono::system_clock::now();
        
        result.block_artifact = blockDetect(mat);
        result.luminance = brightnessDetect(mat);
        result.noise = snowNoiseDetect(mat);
        result.sharpness = sharpnessDetect(mat);
        auto end = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        printf("ms = %lld\n", ms);
        result.dump();
    }
    cv::Mat avframeToCvmat(const AVFrame * frame)  
    {  
        int width = frame->width;  
        int height = frame->height;  
        cv::Mat image(height, width, CV_8UC3); 
        int cvLinesizes[1];  
        cvLinesizes[0] = image.step1();
        SwsContext* conversion = sws_getContext(width, height, (AVPixelFormat) frame->format, width, height, AVPixelFormat::AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);  
        sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data, cvLinesizes);  
        sws_freeContext(conversion);
        return image;  
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
    private:
    result_cb_t result_cb;//分析结果回调
};
#endif