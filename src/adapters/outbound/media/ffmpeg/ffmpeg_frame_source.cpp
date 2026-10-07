#include "adapters/outbound/media/ffmpeg/ffmpeg_frame_source.hpp"

#include <optional>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "common/log.hpp"
#include "common/uri.hpp"

namespace vqec::adapters::ffmpeg {
struct FfmpegFrameSource::Impl {
    std::string uri;
    std::chrono::milliseconds timeout;
    const std::atomic<bool>* stop = nullptr;
    std::chrono::steady_clock::time_point deadline{};
    AVFormatContext* format = nullptr;
    AVCodecContext* decoder = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* scaler = nullptr;
    int stream_index = -1;
    ports::SourceInfo source_info;
    std::optional<std::int64_t> first_pts;
    std::uint64_t sequence = 0;
    bool draining = false;

    static int interrupt(void* opaque) {
        const auto& state = *static_cast<Impl*>(opaque);
        return (state.stop && state.stop->load()) ||
               std::chrono::steady_clock::now() >= state.deadline;
    }
    void arm_deadline() { deadline = std::chrono::steady_clock::now() + timeout; }
    ~Impl() { release(); }
    void release() {
        sws_freeContext(scaler);
        scaler = nullptr;
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
        avformat_close_input(&format);
        first_pts.reset();
        sequence = 0;
        draining = false;
    }
};

FfmpegFrameSource::FfmpegFrameSource(std::string uri, std::chrono::milliseconds io_timeout,
                                     const std::atomic<bool>* stop_requested)
    : impl_(std::make_unique<Impl>()) {
    impl_->uri = std::move(uri);
    impl_->timeout = io_timeout;
    impl_->stop = stop_requested;
    // Default vendor logs can expose credential-bearing URIs; use the VQEC facade instead.
    av_log_set_level(AV_LOG_QUIET);
}
FfmpegFrameSource::~FfmpegFrameSource() = default;
ports::SourceInfo FfmpegFrameSource::info() const {
    return impl_->source_info;
}
void FfmpegFrameSource::close() {
    if (impl_->format)
        VQEC_LOG_INFO("[adapter=ffmpeg_source] source closed");
    impl_->release();
}

Status FfmpegFrameSource::open() {
    close();
    try {
        impl_->arm_deadline();
        impl_->format = avformat_alloc_context();
        if (!impl_->format)
            return make_error(ErrorCode::Internal, "ffmpeg_source: allocation failed");
        impl_->format->interrupt_callback = {Impl::interrupt, impl_.get()};
        AVDictionary* options = nullptr;
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "fflags", "nobuffer", 0);
        const auto timeout_us =
            std::chrono::duration_cast<std::chrono::microseconds>(impl_->timeout).count();
        av_dict_set_int(&options, "stimeout", timeout_us, 0);
        const int opened =
            avformat_open_input(&impl_->format, impl_->uri.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (opened < 0) {
            close();
            return make_error(ErrorCode::Unavailable, "ffmpeg_source: RTSP open failed");
        }
        impl_->arm_deadline();
        if (avformat_find_stream_info(impl_->format, nullptr) < 0) {
            close();
            return make_error(ErrorCode::Unavailable, "ffmpeg_source: stream discovery failed");
        }
        impl_->stream_index =
            av_find_best_stream(impl_->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (impl_->stream_index < 0) {
            close();
            return make_error(ErrorCode::Unsupported, "ffmpeg_source: no video stream");
        }
        const auto* stream = impl_->format->streams[impl_->stream_index];
        const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            close();
            return make_error(ErrorCode::Unsupported, "ffmpeg_source: decoder unavailable");
        }
        impl_->decoder = avcodec_alloc_context3(codec);
        if (!impl_->decoder ||
            avcodec_parameters_to_context(impl_->decoder, stream->codecpar) < 0) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_source: decoder initialization failed");
        }
        impl_->decoder->thread_count = 1;
        if (avcodec_open2(impl_->decoder, codec, nullptr) < 0) {
            close();
            return make_error(ErrorCode::Unsupported, "ffmpeg_source: decoder open failed");
        }
        impl_->frame = av_frame_alloc();
        impl_->packet = av_packet_alloc();
        if (!impl_->frame || !impl_->packet) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_source: frame allocation failed");
        }
        auto fps =
            av_q2d(av_guess_frame_rate(impl_->format, const_cast<AVStream*>(stream), nullptr));
        if (fps <= 0.0)
            fps = 25.0;
        impl_->source_info = {impl_->uri, impl_->decoder->width, impl_->decoder->height, fps,
                              impl_->uri.rfind("rtsp", 0) == 0};
        VQEC_LOG_INFO("[adapter=ffmpeg_source] opened source={} width={} height={} fps={}",
                      redact_uri(impl_->uri), impl_->source_info.width, impl_->source_info.height,
                      fps);
        return {};
    } catch (const std::exception&) {
        close();
        return make_error(ErrorCode::Internal, "ffmpeg_source: open failed");
    }
}

Result<domain::Frame> FfmpegFrameSource::read() {
    if (!impl_->decoder)
        return make_error(ErrorCode::Unavailable, "ffmpeg_source: source is closed");
    try {
        impl_->arm_deadline();
        while (true) {
            const int received = avcodec_receive_frame(impl_->decoder, impl_->frame);
            if (received == 0)
                break;
            if (received == AVERROR_EOF)
                return make_error(ErrorCode::EndOfStream, "ffmpeg_source: end of stream");
            if (received != AVERROR(EAGAIN))
                return make_error(ErrorCode::Unavailable, "ffmpeg_source: decode failed");
            av_packet_unref(impl_->packet);
            const int read_status = av_read_frame(impl_->format, impl_->packet);
            if (read_status == AVERROR_EOF && !impl_->draining) {
                impl_->draining = true;
                if (avcodec_send_packet(impl_->decoder, nullptr) < 0)
                    return make_error(ErrorCode::EndOfStream, "ffmpeg_source: end of stream");
                continue;
            }
            if (read_status < 0)
                return make_error(ErrorCode::Unavailable,
                                  "ffmpeg_source: RTSP read failed or timed out");
            if (impl_->packet->stream_index != impl_->stream_index)
                continue;
            if (avcodec_send_packet(impl_->decoder, impl_->packet) < 0)
                return make_error(ErrorCode::Unavailable, "ffmpeg_source: invalid encoded packet");
        }
        const int width = impl_->frame->width;
        const int height = impl_->frame->height;
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
            return make_error(ErrorCode::Unsupported, "ffmpeg_source: unsupported dimensions");
        auto pixels = std::make_shared<domain::ImageBuffer>();
        pixels->width = width;
        pixels->height = height;
        pixels->stride_bytes = width * 3;
        pixels->data.resize(static_cast<std::size_t>(pixels->stride_bytes) *
                            static_cast<std::size_t>(height));
        impl_->scaler = sws_getCachedContext(
            impl_->scaler, width, height, static_cast<AVPixelFormat>(impl_->frame->format), width,
            height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!impl_->scaler)
            return make_error(ErrorCode::Internal, "ffmpeg_source: pixel converter unavailable");
        std::uint8_t* planes[]{pixels->data.data(), nullptr, nullptr, nullptr};
        int strides[]{pixels->stride_bytes, 0, 0, 0};
        if (sws_scale(impl_->scaler, impl_->frame->data, impl_->frame->linesize, 0, height, planes,
                      strides) != height)
            return make_error(ErrorCode::Internal, "ffmpeg_source: pixel conversion failed");
        domain::Frame output;
        output.width = width;
        output.height = height;
        output.image = std::move(pixels);
        output.sequence = impl_->sequence++;
        output.captured_at = std::chrono::system_clock::now();
        const auto pts = impl_->frame->best_effort_timestamp;
        if (pts != AV_NOPTS_VALUE) {
            if (!impl_->first_pts)
                impl_->first_pts = pts;
            const auto time_base = impl_->format->streams[impl_->stream_index]->time_base;
            output.pts = std::chrono::nanoseconds(
                av_rescale_q(pts - *impl_->first_pts, time_base, AVRational{1, 1000000000}));
        } else {
            output.pts =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(
                    static_cast<double>(output.sequence) / impl_->source_info.fps));
        }
        return output;
    } catch (const std::exception&) {
        return make_error(ErrorCode::Internal, "ffmpeg_source: frame conversion failed");
    }
}
}  // namespace vqec::adapters::ffmpeg
