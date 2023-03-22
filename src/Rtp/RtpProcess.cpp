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
#include "RtpProcess.h"
#include "Http/HttpTSPlayer.h"

using namespace std;
using namespace toolkit;

static constexpr char kRtpAppName[] = "rtp";
//在创建_muxer对象前(也就是推流鉴权成功前)，需要先缓存frame，这样可以防止丢包，提高体验
//但是同时需要控制缓冲长度，防止内存溢出。200帧数据，大概有10秒数据，应该足矣等待鉴权hook返回
static constexpr size_t kMaxCachedFrame = 200;

namespace mediakit {

RtpProcess::RtpProcess(const string &stream_id) {
    _media_info._schema = kRtpAppName;
    _media_info._vhost = DEFAULT_VHOST;
    _media_info._app = kRtpAppName;
    _media_info._streamid = stream_id;

    GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
    {
        FILE *fp = !dump_dir.empty() ? File::create_file(File::absolutePath(_media_info._streamid + ".rtp", dump_dir).data(), "wb") : nullptr;
        if (fp) {
            _save_file_rtp.reset(fp, [](FILE *fp) {
                fclose(fp);
            });
        }
    }

    {
        FILE *fp = !dump_dir.empty() ? File::create_file(File::absolutePath(_media_info._streamid + ".video", dump_dir).data(), "wb") : nullptr;
        if (fp) {
            _save_file_video.reset(fp, [](FILE *fp) {
                fclose(fp);
            });
        }
    }
}

RtpProcess::~RtpProcess() {
    uint64_t duration = (_last_frame_time.createdTime() - _last_frame_time.elapsedTime()) / 1000;
    WarnP(this) << "RTP推流器("
                << _media_info.shortUrl()
                << ")断开,耗时(s):" << duration;

    //流量统计事件广播
    GET_CONFIG(uint32_t, iFlowThreshold, General::kFlowThreshold);
    if (_total_bytes >= iFlowThreshold * 1024) {
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastFlowReport, _media_info, _total_bytes, duration, false, static_cast<SockInfo &>(*this));
    }
}

bool RtpProcess::inputRtp(bool is_udp, const Socket::Ptr &sock, const char *data, size_t len, const struct sockaddr *addr, uint64_t *dts_out) {
    auto is_busy = _busy_flag.test_and_set();
    if (is_busy) {
        //其他线程正在执行本函数
        WarnP(this) << "其他线程正在执行本函数";
        return false;
    }
    //没有其他线程执行本函数
    onceToken token(nullptr, [&]() {
        //本函数执行完毕时，释放状态
        _busy_flag.clear();
    });

    if (!_sock) {
        //第一次运行本函数
        _sock = sock;
        _addr.reset(new sockaddr_storage(*((sockaddr_storage *)addr)));
        emitOnPublish();
    }

    _total_bytes += len;
    if (_save_file_rtp) {
        uint16_t size = (uint16_t)len;
        size = htons(size);
        fwrite((uint8_t *) &size, 2, 1, _save_file_rtp.get());
        fwrite((uint8_t *) data, len, 1, _save_file_rtp.get());
    }
    if (!_process) {
        _process = std::make_shared<GB28181Process>(_media_info, this);
    }

    auto header = (RtpHeader *) data;
    onRtp(ntohs(header->seq), ntohl(header->stamp), 0/*不发送sr,所以可以设置为0*/ , 90000/*ps/ts流时间戳按照90K采样率*/, len);

    GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
    if (_muxer && !_muxer->isEnabled() && !dts_out && dump_dir.empty()) {
        //无人访问、且不取时间戳、不导出调试文件时，我们可以直接丢弃数据
        _last_frame_time.resetTime();
        return false;
    }

    bool ret = _process ? _process->inputRtp(is_udp, data, len) : false;
    if (dts_out) {
        *dts_out = _dts;
    }
    return ret;
}

bool RtpProcess::inputFrame(const Frame::Ptr &frame) {
    if(iqa_cb && frame->getTrackType() == TrackVideo){
        IQAResult result;
        std::string msg;
        int32_t ret = iqa_exec(frame, result, msg);
        if(ret == 0){
            if(_process){
                float m1, m5, total;
                _process->get_packet_loss(m1,m5, total);
                result.loss_pkt_rate_total = total;
            }
            iqa_cb(result, ret, msg);
            uninit_iqa();
        }
    }
    _dts = frame->dts();
    if (_save_file_video && frame->getTrackType() == TrackVideo) {
        fwrite((uint8_t *) frame->data(), frame->size(), 1, _save_file_video.get());
    }
    if (_muxer) {
        _last_frame_time.resetTime();
        return _muxer->inputFrame(frame);
    }
    if (_cached_func.size() > kMaxCachedFrame) {
        WarnL << "cached frame of track(" << frame->getCodecName() << ") is too much, now dropped, please check your on_publish hook url in config.ini file";
        return false;
    }
    auto frame_cached = Frame::getCacheAbleFrame(frame);
    lock_guard<recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, frame_cached]() {
        _last_frame_time.resetTime();
        _muxer->inputFrame(frame_cached);
    });
    return true;
}

bool RtpProcess::addTrack(const Track::Ptr &track) {
    if (_muxer) {
        if(track->getTrackType() == TrackType::TrackVideo){
            auto cid = track->getCodecId();
            switch(cid){
            case CodecId::CodecH264:ffcodec_id = AV_CODEC_ID_H264;break;
            case CodecId::CodecH265:ffcodec_id = AV_CODEC_ID_H265;break;
            default: ffcodec_id = AV_CODEC_ID_NONE;break;
            }
        }
        return _muxer->addTrack(track);
    }
    lock_guard<recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, track]() {
        _muxer->addTrack(track);
    });
    return true;
}

void RtpProcess::addTrackCompleted() {
    if (_muxer) {
        _muxer->addTrackCompleted();
    } else {
        lock_guard<recursive_mutex> lck(_func_mtx);
        _cached_func.emplace_back([this]() {
            _muxer->addTrackCompleted();
        });
    }
}

void RtpProcess::doCachedFunc() {
    lock_guard<recursive_mutex> lck(_func_mtx);
    for (auto &func : _cached_func) {
        func();
    }
    _cached_func.clear();
}

bool RtpProcess::alive() {
    if (_stop_rtp_check.load()) {
        if(_last_check_alive.elapsedTime() > 5 * 60 * 1000){
            //最多暂停5分钟的rtp超时检测，因为NAT映射有效期一般不会太长
            _stop_rtp_check = false;
        } else {
            return true;
        }
    }

    _last_check_alive.resetTime();
    GET_CONFIG(uint64_t, timeoutSec, RtpProxy::kTimeoutSec)
    if (_last_frame_time.elapsedTime() / 1000 < timeoutSec) {
        return true;
    }
    return false;
}

void RtpProcess::setStopCheckRtp(bool is_check){
    _stop_rtp_check = is_check;
    if (!is_check) {
        _last_frame_time.resetTime();
    }
}

void RtpProcess::onDetach() {
    if (_on_detach) {
        _on_detach();
    }
}

void RtpProcess::setOnDetach(const function<void()> &cb) {
    _on_detach = cb;
}

string RtpProcess::get_peer_ip() {
    if (!_addr) {
        return "::";
    }
    return SockUtil::inet_ntoa((sockaddr *)_addr.get());
}

uint16_t RtpProcess::get_peer_port() {
    if (!_addr) {
        return 0;
    }
    return SockUtil::inet_port((sockaddr *)_addr.get());
}

string RtpProcess::get_local_ip() {
    if (_sock) {
        return _sock->get_local_ip();
    }
    return "::";
}

uint16_t RtpProcess::get_local_port() {
    if (_sock) {
        return _sock->get_local_port();
    }
    return 0;
}

string RtpProcess::getIdentifier() const {
    return _media_info._streamid;
}

void RtpProcess::emitOnPublish() {
    weak_ptr<RtpProcess> weak_self = shared_from_this();
    Broadcast::PublishAuthInvoker invoker = [weak_self](const string &err, const ProtocolOption &option) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        auto poller = strong_self->_sock ? strong_self->_sock->getPoller() : EventPollerPool::Instance().getPoller();
        poller->async([weak_self, err, option]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            if (err.empty()) {
                strong_self->_muxer = std::make_shared<MultiMediaSourceMuxer>(strong_self->_media_info._vhost,
                                                                              strong_self->_media_info._app,
                                                                              strong_self->_media_info._streamid,0.0f,
                                                                              option);
                strong_self->_muxer->setMediaListener(strong_self);
                strong_self->doCachedFunc();
                InfoP(strong_self) << "允许RTP推流";
            } else {
                WarnP(strong_self) << "禁止RTP推流:" << err;
            }
        });
    };

    //触发推流鉴权事件
    auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastMediaPublish, MediaOriginType::rtp_push, _media_info, invoker, static_cast<SockInfo &>(*this));
    if (!flag) {
        //该事件无人监听,默认不鉴权
        invoker("", ProtocolOption());
    }
}

MediaOriginType RtpProcess::getOriginType(MediaSource &sender) const{
    return MediaOriginType::rtp_push;
}

string RtpProcess::getOriginUrl(MediaSource &sender) const {
    return _media_info.getUrl();
}

std::shared_ptr<SockInfo> RtpProcess::getOriginSock(MediaSource &sender) const {
    return const_cast<RtpProcess *>(this)->shared_from_this();
}

toolkit::EventPoller::Ptr RtpProcess::getOwnerPoller(MediaSource &sender) {
    return _sock ? _sock->getPoller() : EventPollerPool::Instance().getPoller();
}

float RtpProcess::getLossRate(MediaSource &sender, TrackType type) {
    auto expected = getExpectedPacketsInterval();
    if (!expected) {
        return -1;
    }
    return geLostInterval() * 100 / expected;
}

int32_t RtpProcess::iqa_exec(const Frame::Ptr& frame, IQAResult &result, std::string &msg) {
    if(iqa_ptr == nullptr){
            msg = "内部错误, 无效的分析器";
            return -1;
    }
        if (frame->getTrackType() != TrackVideo) {
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
        return iqa_ptr->parse_frame(avframe, result);
    }
    int32_t RtpProcess::init_iqa(std::string& msg){
        if (ffcodec_id == AVCodecID::AV_CODEC_ID_NONE) {
            msg = "无法获取视频编码";
            return -1;
        }
        //初始化解码器和解析器
        decoder_ptr = std::make_shared<cff::av_decoder_context_t>(ffcodec_id);
        assert(*(decoder_ptr));
        if(!(*(decoder_ptr))){
            msg = "创建解码器失败";
            goto err;
        }
        packet_parser_ptr = std::make_shared<cff::avcodec_parser_t>(ffcodec_id);
        if (packet_parser_ptr == nullptr) {
            msg = "创建packet-parser失败";
            goto err;
        }
        iqa_ptr = std::make_shared<IQA>();
        return 0;
        err:
        uninit_iqa();
        return -1;
    }
    void RtpProcess::uninit_iqa(){
        iqa_cb = nullptr;
        iqa_ptr.reset();
        decoder_ptr.reset();
        packet_parser_ptr.reset();
    }

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)