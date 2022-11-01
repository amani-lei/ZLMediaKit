/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_RTPPROXY)
#include "GB28181Process.h"
#include "Extension/CommonRtp.h"
#include "Extension/Factory.h"
#include "Extension/G711.h"
#include "Extension/H264Rtp.h"
#include "Extension/H265.h"
#include "Extension/Opus.h"
#include "Http/HttpTSPlayer.h"
#include "Util/File.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

// 判断是否为ts负载
static inline bool checkTS(const uint8_t *packet, size_t bytes) {
    return bytes % TS_PACKET_SIZE == 0 && packet[0] == TS_SYNC_BYTE;
}

class RtpReceiverImp : public RtpTrackImp {
public:
    using Ptr = std::shared_ptr<RtpReceiverImp>;

    RtpReceiverImp(int sample_rate, RtpTrackImp::OnSorted cb, RtpTrackImp::BeforeSorted cb_before = nullptr) {
        _sample_rate = sample_rate;
        setOnSorted(std::move(cb));
        setBeforeSorted(std::move(cb_before));
        // GB28181推流不支持ntp时间戳
        setNtpStamp(0, 0);
    }

    virtual ~RtpReceiverImp() override = default;

    bool inputRtp(TrackType type, uint8_t *ptr, size_t len) {
        return RtpTrack::inputRtp(type, _sample_rate, ptr, len).operator bool();
    }

private:
    int _sample_rate;
};

///////////////////////////////////////////////////////////////////////////////////////////

GB28181Process::GB28181Process(const MediaInfo &media_info, MediaSinkInterface *sink) {
    assert(sink);
    _media_info = media_info;
    _interface = sink;
}

GB28181Process::~GB28181Process() {}

void GB28181Process::onRtpSorted(RtpPacket::Ptr rtp) {
    static int recv = 0;
    static int loss = 0;
    static int32_t next_seq = -1;
    recv++;

    int32_t seq = rtp->getSeq();

    if(next_seq<0){
        next_seq = (rtp->getSeq() + 1) % 65536;
    }else if(next_seq > seq){
        loss += seq + 65535 - next_seq;
    }else{
        loss += seq - next_seq;
    }
    next_seq = seq + 1;

    printf("seq = %d, loss = %.2f%%\n", seq, (loss*100.0) / (float)(recv + loss));
    _rtp_decoder[rtp->getHeader()->pt]->inputRtp(rtp, false);
}

bool GB28181Process::inputRtp(bool, const char *data, size_t data_len) {
    static int xxx = 1;
    if((xxx++ % 30) == 0){
        return true;
    }
    GET_CONFIG(uint32_t, h264_pt, RtpProxy::KH264PT);
    GET_CONFIG(uint32_t, h265_pt, RtpProxy::KH265PT);
    GET_CONFIG(uint32_t, ps_pt, RtpProxy::KPSPT);
    GET_CONFIG(uint32_t, ts_pt, RtpProxy::KTSPT);
    GET_CONFIG(uint32_t, opus_pt, RtpProxy::KOpusPT);
    GET_CONFIG(uint32_t, g711u_pt, RtpProxy::KG711UPT);
    GET_CONFIG(uint32_t, g711a_pt, RtpProxy::KG711APT);

    RtpHeader *header = (RtpHeader *)data;
    auto pt = header->pt;
    if (pt == 40) {
        return false;
    }
    auto &ref = _rtp_receiver[pt];
    if (!ref) {
        if (_rtp_receiver.size() > 2) {
            // 防止pt类型太多导致内存溢出
            throw std::invalid_argument("rtp pt类型不得超过2种!");
        }
        if (pt == opus_pt) {
            // opus负载
            ref = std::make_shared<RtpReceiverImp>(48000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });

            auto track = std::make_shared<OpusTrack>();
            _interface->addTrack(track);
            _rtp_decoder[pt] = Factory::getRtpDecoderByTrack(track);
        } else if (pt == h265_pt) {
            // H265负载
            ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });

            auto track = std::make_shared<H265Track>();
            _interface->addTrack(track);
            _rtp_decoder[pt] = Factory::getRtpDecoderByTrack(track);
        } else if (pt == h264_pt) {
            // H264负载
            ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });

            auto track = std::make_shared<H264Track>();
            _interface->addTrack(track);
            _rtp_decoder[pt] = Factory::getRtpDecoderByTrack(track);
        } else if (pt == g711u_pt || pt == g711a_pt) {
            // CodecG711U
            // CodecG711A
            ref = std::make_shared<RtpReceiverImp>(8000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });

            auto track = std::make_shared<G711Track>(pt == 0 ? CodecG711U : CodecG711A, 8000, 1, 16);
            _interface->addTrack(track);
            _rtp_decoder[pt] = Factory::getRtpDecoderByTrack(track);
        } else {
            //大华rtp私有
            uint8_t *payload = header->getPayloadData();
            if (memcmp(payload, "DHAV", 4) == 0) {
                WarnL << "rtp payload type推测为大华非标40(" << (int)pt << ")";
                ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });
                _rtp_decoder[96] = std::shared_ptr<CommonRtpDecoder>((CommonRtpDecoder *)(new DahuaRtpDecoder(CodecInvalid, 32 * 1024)));
            }else{
                if (pt != 33 && pt != 96) {
                WarnL << "rtp payload type未识别(" << (int)pt << "),已按ts或ps负载处理";
                }
                ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });
                // ts或ps负载
                _rtp_decoder[pt] = std::make_shared<CommonRtpDecoder>(CodecInvalid, 32 * 1024);
            }
            // 设置dump目录
            GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
            if (!dump_dir.empty()) {
                auto save_path = File::absolutePath(_media_info._streamid + ".mp2", dump_dir);
                _save_file_ps.reset(File::create_file(save_path.data(), "wb"), [](FILE *fp) {
                    if (fp) {
                        fclose(fp);
                    }
                });
            }
        }
        // 设置frame回调
        _rtp_decoder[pt]->addDelegate(std::make_shared<FrameWriterInterfaceHelper>([this](const Frame::Ptr &frame) {
            onRtpDecode(frame);
            return true;
        }));
    }

    return ref->inputRtp(TrackVideo, (unsigned char *)data, data_len);
}

void GB28181Process::onRtpDecode(const Frame::Ptr &frame) {
    if (frame->getCodecId() != CodecInvalid) {
        // 这里不是ps或ts
        _interface->inputFrame(frame);
        return;
    }

    // 这是TS或PS
    if (_save_file_ps) {
        fwrite(frame->data(), frame->size(), 1, _save_file_ps.get());
    }

    if (!_decoder) {
        // 创建解码器
        if (checkTS((uint8_t *)frame->data(), frame->size())) {
            // 猜测是ts负载
            InfoL << _media_info._streamid << " judged to be TS";
            _decoder = DecoderImp::createDecoder(DecoderImp::decoder_ts, _interface);
        } else {
            // 猜测是ps负载
            InfoL << _media_info._streamid << " judged to be PS";
            _decoder = DecoderImp::createDecoder(DecoderImp::decoder_ps, _interface);
        }
    }

    if (_decoder) {
        _decoder->input(reinterpret_cast<const uint8_t *>(frame->data()), frame->size());
    }
}

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
