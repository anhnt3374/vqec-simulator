#include "adapters/outbound/media/ffmpeg/ffmpeg_frame_sink.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "adapters/outbound/media/ffmpeg/overlay_renderer.hpp"
#include "common/bounded_queue.hpp"
#include "common/log.hpp"
#include "common/uri.hpp"

namespace vqec::adapters::ffmpeg {
struct FfmpegFrameSink::Impl {
    struct Job {
        domain::Frame frame;
        ports::FrameAnnotations annotations;
    };
    FfmpegSinkOptions options;
    std::unique_ptr<BoundedQueue<Job>> queue;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::atomic<bool> failed{false};
    std::chrono::steady_clock::time_point deadline{};
    AVFormatContext* format = nullptr;
    AVCodecContext* encoder = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    AVStream* stream = nullptr;  // Non-owning; owned by format.
    EncoderClock clock;
    bool header_written = false;

    static int interrupt(void* opaque) {
        const auto& state = *static_cast<Impl*>(opaque);
        return state.stopping.load() || std::chrono::steady_clock::now() >= state.deadline;
    }
    void arm_deadline() { deadline = std::chrono::steady_clock::now() + options.io_timeout; }
    void release() {
        if (header_written && format) {
            arm_deadline();
            av_write_trailer(format);
        }
        header_written = false;
        sws_freeContext(scaler);
        scaler = nullptr;
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
        if (format) {
            if (!(format->oformat->flags & AVFMT_NOFILE))
                avio_closep(&format->pb);
            avformat_free_context(format);
            format = nullptr;
        }
        stream = nullptr;
    }
    Status publish(const Job& job) {
        auto image = render_overlay(job.frame, job.annotations, options.width, options.height);
        if (!image.ok())
            return image.status();
        if (av_frame_make_writable(frame) < 0)
            return make_error(ErrorCode::Internal, "ffmpeg_sink: output buffer unavailable");
        const std::uint8_t* source[]{image.value()->data.data(), nullptr, nullptr, nullptr};
        int strides[]{image.value()->stride_bytes, 0, 0, 0};
        if (sws_scale(scaler, source, strides, 0, options.height, frame->data, frame->linesize) !=
            options.height)
            return make_error(ErrorCode::Internal, "ffmpeg_sink: pixel conversion failed");
        frame->pts = clock.next(job.frame.pts);
        if (avcodec_send_frame(encoder, frame) < 0)
            return make_error(ErrorCode::Internal, "ffmpeg_sink: encoding failed");
        while (true) {
            const int received = avcodec_receive_packet(encoder, packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                return {};
            if (received < 0)
                return make_error(ErrorCode::Internal, "ffmpeg_sink: packet encode failed");
            av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
            packet->stream_index = stream->index;
            arm_deadline();
            const int written = av_interleaved_write_frame(format, packet);
            av_packet_unref(packet);
            if (written < 0)
                return make_error(ErrorCode::Unavailable,
                                  "ffmpeg_sink: RTSP publish failed or timed out");
        }
    }
    void run() {
        auto started = std::chrono::steady_clock::now();
        std::uint64_t count = 0;
        try {
            while (auto job = queue->pop()) {
                if (stopping.load())
                    break;
                const auto status = publish(*job);
                if (!status.ok()) {
                    if (!stopping.load()) {
                        failed.store(true);
                        VQEC_LOG_ERROR("[adapter=ffmpeg_sink] publisher stopped code={} message={}",
                                       to_string(status.error().code), status.error().message);
                    }
                    break;
                }
                ++count;
                const auto now = std::chrono::steady_clock::now();
                if (now - started >= options.stats_interval) {
                    const double seconds = std::chrono::duration<double>(now - started).count();
                    VQEC_LOG_INFO(
                        "[adapter=ffmpeg_sink] output_fps={:.2f} published={} queue_depth={} "
                        "dropped={}",
                        static_cast<double>(count) / seconds, count, queue->size(),
                        queue->dropped_count());
                    started = now;
                    count = 0;
                }
            }
        } catch (const std::exception&) {
            failed.store(true);
            VQEC_LOG_ERROR(
                "[adapter=ffmpeg_sink] publisher stopped code=internal message=unexpected "
                "publisher failure");
        } catch (...) {
            failed.store(true);
            VQEC_LOG_ERROR(
                "[adapter=ffmpeg_sink] publisher stopped code=internal "
                "message=unknown publisher failure");
        }
        queue->close();
    }
};

FfmpegFrameSink::FfmpegFrameSink(FfmpegSinkOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
}
FfmpegFrameSink::~FfmpegFrameSink() {
    close();
}
void FfmpegFrameSink::close() {
    impl_->stopping.store(true);
    if (impl_->queue)
        impl_->queue->close();
    if (impl_->worker.joinable())
        impl_->worker.join();
    if (impl_->header_written)
        VQEC_LOG_INFO("[adapter=ffmpeg_sink] publisher closed");
    impl_->release();
}

Status FfmpegFrameSink::open(const ports::SourceInfo& /*source_info*/) {
    close();
    impl_->stopping.store(false);
    impl_->failed.store(false);
    impl_->clock = EncoderClock{};
    try {
        const auto* codec = avcodec_find_encoder_by_name("libx264");
        if (!codec)
            return make_error(ErrorCode::Unsupported, "ffmpeg_sink: libx264 encoder unavailable");
        if (avformat_alloc_output_context2(&impl_->format, nullptr, "rtsp",
                                           impl_->options.uri.c_str()) < 0 ||
            !impl_->format)
            return make_error(ErrorCode::Internal,
                              "ffmpeg_sink: RTSP output initialization failed");
        impl_->format->interrupt_callback = {Impl::interrupt, impl_.get()};
        impl_->encoder = avcodec_alloc_context3(codec);
        impl_->stream = avformat_new_stream(impl_->format, nullptr);
        if (!impl_->encoder || !impl_->stream) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_sink: encoder allocation failed");
        }
        auto& encoder = *impl_->encoder;
        encoder.width = impl_->options.width;
        encoder.height = impl_->options.height;
        encoder.pix_fmt = AV_PIX_FMT_YUV420P;
        encoder.time_base = {1, 90000};
        encoder.framerate = {impl_->options.fps, 1};
        encoder.gop_size = impl_->options.fps;
        encoder.max_b_frames = 0;
        encoder.thread_count = 1;
        if (impl_->format->oformat->flags & AVFMT_GLOBALHEADER)
            encoder.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* options = nullptr;
        av_dict_set(&options, "preset", "ultrafast", 0);
        av_dict_set(&options, "tune", "zerolatency", 0);
        av_dict_set(&options, "crf", "23", 0);
        const int opened = avcodec_open2(impl_->encoder, codec, &options);
        av_dict_free(&options);
        if (opened < 0) {
            close();
            return make_error(ErrorCode::Unsupported, "ffmpeg_sink: H.264 encoder open failed");
        }
        if (avcodec_parameters_from_context(impl_->stream->codecpar, impl_->encoder) < 0) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_sink: encoder parameters failed");
        }
        impl_->stream->time_base = encoder.time_base;
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        impl_->arm_deadline();
        const int written = avformat_write_header(impl_->format, &options);
        av_dict_free(&options);
        if (written < 0) {
            close();
            return make_error(ErrorCode::Unavailable, "ffmpeg_sink: RTSP publisher connect failed");
        }
        impl_->header_written = true;
        impl_->frame = av_frame_alloc();
        impl_->packet = av_packet_alloc();
        if (!impl_->frame || !impl_->packet) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_sink: frame allocation failed");
        }
        impl_->frame->format = encoder.pix_fmt;
        impl_->frame->width = encoder.width;
        impl_->frame->height = encoder.height;
        if (av_frame_get_buffer(impl_->frame, 32) < 0) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_sink: image allocation failed");
        }
        impl_->scaler = sws_getContext(encoder.width, encoder.height, AV_PIX_FMT_BGR24,
                                       encoder.width, encoder.height, encoder.pix_fmt, SWS_BILINEAR,
                                       nullptr, nullptr, nullptr);
        if (!impl_->scaler) {
            close();
            return make_error(ErrorCode::Internal, "ffmpeg_sink: pixel converter unavailable");
        }
        impl_->queue = std::make_unique<BoundedQueue<Impl::Job>>(impl_->options.queue_capacity,
                                                                 OverflowPolicy::DropOldest);
        impl_->worker = std::thread([this] { impl_->run(); });
        VQEC_LOG_INFO(
            "[adapter=ffmpeg_sink] publisher opened output={} codec=h264 dimensions={}x{} "
            "nominal_fps={}",
            redact_uri(impl_->options.uri), encoder.width, encoder.height, impl_->options.fps);
        return {};
    } catch (const std::exception&) {
        close();
        return make_error(ErrorCode::Internal, "ffmpeg_sink: publisher initialization failed");
    }
}

Status FfmpegFrameSink::write(const domain::Frame& frame,
                              const ports::FrameAnnotations& annotations) {
    try {
        if (!impl_->queue || impl_->failed.load() || !impl_->queue->push({frame, annotations}))
            return make_error(ErrorCode::Unavailable, "ffmpeg_sink: publisher is unusable");
        return {};
    } catch (...) {
        return make_error(ErrorCode::Internal, "ffmpeg_sink: output queue failed");
    }
}
}  // namespace vqec::adapters::ffmpeg
