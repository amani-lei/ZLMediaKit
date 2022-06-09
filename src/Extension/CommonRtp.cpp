/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "CommonRtp.h"
#include "pspacket.h"

using namespace mediakit;

CommonRtpDecoder::CommonRtpDecoder(CodecId codec, size_t max_frame_size) {
    _codec = codec;
    _max_frame_size = max_frame_size;
    obtainFrame();
}

CodecId CommonRtpDecoder::getCodecId() const {
    return _codec;
}

void CommonRtpDecoder::obtainFrame() {
    _frame = FrameImp::create();
    _frame->_codec_id = _codec;
}
DahuaRtpDecoder::DahuaRtpDecoder(CodecId codec, size_t max_frame_size)
    : CommonRtpDecoder(codec, max_frame_size) {
    temp_buffer.reserve(1024 * 1024 * 5);
}

bool CommonRtpDecoder::inputRtp(const RtpPacket::Ptr &rtp, bool) {
    auto payload_size = rtp->getPayloadSize();
    if (payload_size <= 0) {
        //无实际负载
        return false;
    }
    auto payload = rtp->getPayload();
    auto stamp = rtp->getStampMS();
    auto seq = rtp->getSeq();

    if (_frame->_dts != stamp || _frame->_buffer.size() > _max_frame_size) {
        //时间戳发生变化或者缓存超过MAX_FRAME_SIZE，则清空上帧数据
        if (!_frame->_buffer.empty()) {
            //有有效帧，则输出
            RtpCodec::inputFrame(_frame);
        }

        //新的一帧数据
        obtainFrame();
        _frame->_dts = stamp;
        _drop_flag = false;
    } else if (_last_seq != 0 && (uint16_t)(_last_seq + 1) != seq) {
        //时间戳未发生变化，但是seq却不连续，说明中间rtp丢包了，那么整帧应该废弃
        WarnL << "rtp丢包:" << _last_seq << " -> " << seq;
        _drop_flag = true;
        _frame->_buffer.clear();
    }

    if (!_drop_flag) {
        _frame->_buffer.append((char *)payload, payload_size);
    }

    _last_seq = seq;
    return false;
}

////////////////////////////////////////////////////////////////

CommonRtpEncoder::CommonRtpEncoder(
    CodecId codec, uint32_t ssrc, uint32_t mtu_size, uint32_t sample_rate, uint8_t payload_type, uint8_t interleaved)
    : CommonRtpDecoder(codec)
    , RtpInfo(ssrc, mtu_size, sample_rate, payload_type, interleaved) {}

bool CommonRtpEncoder::inputFrame(const Frame::Ptr &frame) {
    auto stamp = frame->pts();
    auto ptr = frame->data() + frame->prefixSize();
    auto len = frame->size() - frame->prefixSize();
    auto remain_size = len;
    auto max_size = getMaxSize();

    bool mark = false;
    while (remain_size > 0) {
        size_t rtp_size;
        if (remain_size > max_size) {
            rtp_size = max_size;
        } else {
            rtp_size = remain_size;
            mark = true;
        }
        RtpCodec::inputRtp(makeRtp(getTrackType(), ptr, rtp_size, mark, stamp), false);
        ptr += rtp_size;
        remain_size -= rtp_size;
    }
    return len > 0;
}

void DahuaRtpDecoder::repacket_ps() {
    ps_buffer.clear();
    //拆分nalu
    uint8_t *ptr = (uint8_t *)buffer.data();
    size_t len = buffer.size();
    int pos = 0;
    uint8_t *nalu_start = ptr;
    uint8_t *nalu_end = nullptr;

    std::vector<std::vector<uint8_t>> other_nalu; //保存这个数据包内的所有nalu
    while (nalu_end != ptr + buffer.size()) {
        nalu_start = find_nalu_start(nalu_start, ptr + len - 1);
        if (nalu_start == nullptr) {
            break;
        }
        nalu_end = find_nalu_end(nalu_start + 4, ptr + len - 1);
        switch (nalu_start[4] & 0x1f) {
        case 7: // sps
        {
            sps.resize(nalu_end - nalu_start + 1);
            memcpy((uint8_t *)sps.data(), nalu_start, sps.size());
            break;
        }
        case 8: // pps
        {
            pps.resize(nalu_end - nalu_start + 1);
            memcpy((uint8_t *)pps.data(), nalu_start, pps.size());
            break;
        }
        //不处理的包
        case 6: {
            break;
        }
        // break;
        //其他默认包
        default: {
            std::vector<uint8_t> nalu;
            nalu.resize(nalu_end - nalu_start + 1);
            memcpy((uint8_t *)nalu.data(), nalu_start, nalu.size());
            other_nalu.push_back(std::move(nalu));
            break;
        }
        }
        nalu_start = nalu_end;
    }
    //封装成ps
    size_t ps_size = 0;
    uint8_t *ps_ptr = (uint8_t *)temp_buffer.data();
    //添加ps头
    ps_size += gb28181_make_ps_header(ps_ptr + ps_size, ssrc);
    for (auto &nalu : other_nalu) {
        //如果是关键帧,在前面添加sps和pps
        if ((nalu[4] & 0x1f) == 5) {
            //添加sys
            ps_size += gb28181_make_sys_header(ps_ptr + ps_size);
            //添加sps
            ps_size += gb28181_make_psm_header(ps_ptr + ps_size);
            ps_size += gb28181_make_pes_header(ps_ptr + ps_size, 0xe0, sps.size(), timestamp, timestamp);
            memcpy(ps_ptr + ps_size, sps.data(), sps.size());
            ps_size += sps.size();
            //添加pps
            ps_size += gb28181_make_psm_header(ps_ptr + ps_size);
            ps_size += gb28181_make_pes_header(ps_ptr + ps_size, 0xe0, pps.size(), timestamp, timestamp);
            memcpy(ps_ptr + ps_size, pps.data(), pps.size());
            ps_size += pps.size();
        }
        //添加本nalu
        size_t left_size = nalu.size();
        size_t read_pos = 0;
        while(left_size > 0){
const size_t ONCE_SIZE = 50000;
            if(left_size >= ONCE_SIZE){
                ps_size += gb28181_make_pes_header(ps_ptr + ps_size, 0xe0, ONCE_SIZE, timestamp, timestamp);
                memcpy(ps_ptr + ps_size, nalu.data() + read_pos, ONCE_SIZE);
                ps_size += ONCE_SIZE;
                left_size -= ONCE_SIZE;
                read_pos += ONCE_SIZE;
            }else{
                ps_size += gb28181_make_pes_header(ps_ptr + ps_size, 0xe0, left_size, timestamp, timestamp);
                memcpy(ps_ptr + ps_size, nalu.data() + read_pos, left_size);
                ps_size += left_size;
                read_pos += left_size;
                left_size = 0;
                break;
            }
        }
    }
    
    ps_buffer.clear();
    ps_buffer.resize(ps_size);
    memcpy((char*)ps_buffer.data(), (char*)temp_buffer.data(), ps_size);
    buffer.clear();
}

bool DahuaRtpDecoder::inputRtp(const RtpPacket::Ptr &rtp, bool) {
    uint8_t *pl = rtp->getPayload();
    bool dh_frame_end = false;
    timestamp = rtp->getStamp();
    size_t rtp_pl_size = rtp->getPayloadSize();
    
    if (memcmp(pl, "HVAG", 4) == 0) {
        return false;
    } else if (memcmp(pl, "DHAV", 4) == 0) {//以DHAV开始的包
        if (pl[4] != 0xfd && pl[4] != 0xfc) {
            return false;
        }
        if (rtp_pl_size > 12 && memcmp(pl + rtp_pl_size - 8, "dhav", 4) == 0) {//并且以dhav结束的包
            buffer.append((char *)pl + 44, rtp_pl_size - 44 - 8);
            dh_frame_end = true;
        } else {
            buffer.append((char *)pl + 44, rtp_pl_size - 44);//无结尾
        }
    } else {
        if (rtp_pl_size > 12 && memcmp(pl + rtp_pl_size - 8, "dhav", 4) == 0) {//不以DHAV开始,但以dhav结束的包
            buffer.append((char *)pl, rtp_pl_size - 8);
            dh_frame_end = true;
        } else {
            buffer.append((char *)pl, rtp_pl_size);//无结尾
        }
    }
    
    if (!dh_frame_end) {
        //"dhav....",有时dhav包尾标记并不在同一个包
        if(buffer.size() > 8 && memcmp(buffer.data() + buffer.size() - 8, "dhav", 4) == 0){
            buffer.erase(buffer.end() - 8, buffer.end());
            dh_frame_end = true;
        }else{
            return false; //未到一帧结束,等待结尾
        }
    }
    //重组ps
    repacket_ps();
    obtainFrame();
    
    _frame->_dts = rtp->getStampMS();
    _frame->_buffer = ps_buffer;

    RtpCodec::inputFrame(_frame);
    return false;
}