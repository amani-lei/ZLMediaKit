/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_COMMONRTP_H
#define ZLMEDIAKIT_COMMONRTP_H

#include "Frame.h"
#include "Rtsp/RtpCodec.h"

namespace mediakit{

/**
 * 通用 rtp解码类
 */
class CommonRtpDecoder : public RtpCodec {
public:
    typedef std::shared_ptr <CommonRtpDecoder> Ptr;

    ~CommonRtpDecoder() override {}

    /**
     * 构造函数
     * @param codec 编码id
     * @param max_frame_size 允许的最大帧大小
     */
    CommonRtpDecoder(CodecId codec, size_t max_frame_size = 2 * 1024);

    /**
     * 返回编码类型ID
     */
    CodecId getCodecId() const override;

    /**
     * 输入rtp并解码
     * @param rtp rtp数据包
     * @param key_pos 此参数内部强制转换为false,请忽略之
     */
    virtual bool inputRtp(const RtpPacket::Ptr &rtp, bool key_pos = false) override;

protected:
    void obtainFrame();

protected:
    bool _drop_flag = false;
    uint16_t _last_seq = 0;
    size_t _max_frame_size;
    CodecId _codec;
    FrameImp::Ptr _frame;
};

class DahuaRtpDecoder:public CommonRtpDecoder{
    public:
        /**
     * 构造函数
     * @param codec 编码id
     * @param max_frame_size 允许的最大帧大小
     */
    DahuaRtpDecoder(CodecId codec, size_t max_frame_size = 2 * 1024);
    virtual bool inputRtp(const RtpPacket::Ptr &rtp, bool key_pos = false) override;
    inline uint8_t *find_nalu_start(uint8_t *start, uint8_t *end) {
        uint8_t *nalu_start = nullptr;
        while (end - start >= 3) {
            if (start[0] == 0x00 && start[1] == 0x00 && start[2] == 0x00 && start[3] == 0x01) {
                nalu_start = start;
                break;
            }
            start++;
        }
        return nalu_start;
    }
    inline uint8_t *find_nalu_end(uint8_t *start, uint8_t *end) {
        uint8_t *nalu_start = end;
        while (end - start >= 3) {
            if (start[0] == 0x00 && start[1] == 0x00 && start[2] == 0x00 && start[3] == 0x01) {
                nalu_start = start - 1;
                break;
            }
            start++;
        }
        return nalu_start;
    }
    void repacket_ps();
    //void repacket_rtp();
private:
    std::string buffer;//一个完整或多个的nalu
    std::string sps;
    std::string pps;
    std::string temp_buffer;
    std::string ps_buffer;
    uint64_t timestamp;
    uint64_t ssrc;
};

/**
 * 通用 rtp编码类
 */
class CommonRtpEncoder : public CommonRtpDecoder, public RtpInfo {
public:
    typedef std::shared_ptr <CommonRtpEncoder> Ptr;

    ~CommonRtpEncoder() override {}

    /**
     * 构造函数
     * @param codec 编码类型
     * @param ssrc ssrc
     * @param mtu_size mtu 大小
     * @param sample_rate 采样率
     * @param payload_type pt类型
     * @param interleaved rtsp interleaved 值
     */
    CommonRtpEncoder(CodecId codec, uint32_t ssrc, uint32_t mtu_size, uint32_t sample_rate, uint8_t payload_type, uint8_t interleaved);

    /**
     * 输入帧数据并编码成rtp
     */
    bool inputFrame(const Frame::Ptr &frame) override;
};

}//namespace mediakit
#endif //ZLMEDIAKIT_COMMONRTP_H
