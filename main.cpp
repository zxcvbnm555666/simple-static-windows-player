#define SDL_MAIN_HANDLED
#define NOMINMAX

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string ffError(int error)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, text, sizeof(text));
    return text;
}

std::string wideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::string chooseVideoFile()
{
    wchar_t fileName[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrFilter =
        L"视频文件\0*.mp4;*.mkv;*.avi;*.mov;*.flv;*.webm;*.ts;*.m2ts;*.wmv\0"
        L"所有文件\0*.*\0";
    dialog.lpstrTitle = L"选择要播放的视频";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    return GetOpenFileNameW(&dialog) ? wideToUtf8(fileName) : std::string();
}

int printFeatureProbe()
{
    avdevice_register_all();

    bool complete = true;
    const auto report = [&complete](const char* type, const char* name, bool found) {
        std::cout << type << '|' << name << '|' << (found ? "YES" : "NO") << '\n';
        complete = complete && found;
    };

    report("ENCODER", "libx265", avcodec_find_encoder_by_name("libx265") != nullptr);
    report("ENCODER", "libxvid", avcodec_find_encoder_by_name("libxvid") != nullptr);
    report("ENCODER", "h264_amf", avcodec_find_encoder_by_name("h264_amf") != nullptr);
    report("ENCODER", "hevc_amf", avcodec_find_encoder_by_name("hevc_amf") != nullptr);
    report("FILTER", "subtitles", avfilter_get_by_name("subtitles") != nullptr);
    report("FILTER", "drawtext", avfilter_get_by_name("drawtext") != nullptr);
    report("FILTER", "showcqt", avfilter_get_by_name("showcqt") != nullptr);
    report("OUTDEV", "opengl", av_guess_format("opengl", nullptr, nullptr) != nullptr);

    HMODULE amfRuntime = LoadLibraryW(L"amfrt64.dll");
    std::cout << "RUNTIME|amfrt64.dll|" << (amfRuntime ? "YES" : "NO") << '\n';
    if (amfRuntime) {
        FreeLibrary(amfRuntime);
    }
    return complete ? 0 : 2;
}

class VideoPlayer {
public:
    ~VideoPlayer()
    {
        close();
    }

    bool open(const std::string& path)
    {
        int result = avformat_open_input(&format_, path.c_str(), nullptr, nullptr);
        if (result < 0) {
            return fail("无法打开文件", result);
        }

        result = avformat_find_stream_info(format_, nullptr);
        if (result < 0) {
            return fail("无法读取媒体信息", result);
        }

        videoStreamIndex_ = openDecoder(AVMEDIA_TYPE_VIDEO, &videoCodec_);
        if (videoStreamIndex_ < 0) {
            return fail("未找到可解码的视频流", videoStreamIndex_);
        }

        audioStreamIndex_ = openDecoder(AVMEDIA_TYPE_AUDIO, &audioCodec_);
        if (audioStreamIndex_ < 0) {
            std::cerr << "提示：没有可播放的音频流，将仅播放画面。\n";
            audioStreamIndex_ = -1;
        }

        if (!initSdl()) {
            return false;
        }

        packet_ = av_packet_alloc();
        decodedFrame_ = av_frame_alloc();
        displayFrame_ = av_frame_alloc();
        if (!packet_ || !decodedFrame_ || !displayFrame_) {
            return fail("内存分配失败", AVERROR(ENOMEM));
        }

        displayFrame_->format = AV_PIX_FMT_YUV420P;
        displayFrame_->width = videoCodec_->width;
        displayFrame_->height = videoCodec_->height;
        result = av_frame_get_buffer(displayFrame_, 32);
        if (result < 0) {
            return fail("无法分配视频画面缓冲区", result);
        }

        scaler_ = sws_getContext(
            videoCodec_->width, videoCodec_->height, videoCodec_->pix_fmt,
            videoCodec_->width, videoCodec_->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler_) {
            return fail("无法创建图像转换器", AVERROR(EINVAL));
        }

        av_dump_format(format_, 0, path.c_str(), 0);
        return true;
    }

    int play()
    {
        bool running = true;

        while (running && av_read_frame(format_, packet_) >= 0) {
            if (packet_->stream_index == videoStreamIndex_) {
                running = decodePacket(videoCodec_, packet_, true);
            } else if (packet_->stream_index == audioStreamIndex_) {
                running = decodePacket(audioCodec_, packet_, false);
            }
            av_packet_unref(packet_);
            pollEvents(running);
        }

        if (running) {
            decodePacket(videoCodec_, nullptr, true);
            if (audioCodec_) {
                decodePacket(audioCodec_, nullptr, false);
            }
        }

        while (running && audioDevice_ && SDL_GetQueuedAudioSize(audioDevice_) > 0) {
            pollEvents(running);
            SDL_Delay(10);
        }
        return 0;
    }

private:
    bool fail(const char* message, int error)
    {
        std::cerr << message;
        if (error < 0) {
            std::cerr << "：" << ffError(error);
        }
        std::cerr << '\n';
        return false;
    }

    int openDecoder(AVMediaType type, AVCodecContext** output)
    {
        const AVCodec* decoder = nullptr;
        const int streamIndex =
            av_find_best_stream(format_, type, -1, -1, &decoder, 0);
        if (streamIndex < 0) {
            return streamIndex;
        }

        AVCodecContext* context = avcodec_alloc_context3(decoder);
        if (!context) {
            return AVERROR(ENOMEM);
        }

        int result = avcodec_parameters_to_context(
            context, format_->streams[streamIndex]->codecpar);
        if (result >= 0) {
            result = avcodec_open2(context, decoder, nullptr);
        }
        if (result < 0) {
            avcodec_free_context(&context);
            return result;
        }

        *output = context;
        return streamIndex;
    }

    bool initSdl()
    {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
            std::cerr << "SDL 初始化失败：" << SDL_GetError() << '\n';
            return false;
        }
        sdlInitialized_ = true;

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        window_ = SDL_CreateWindow(
            "FFmpeg 静态库播放 Demo",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            std::max(videoCodec_->width, 640),
            std::max(videoCodec_->height, 360),
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window_) {
            std::cerr << "创建窗口失败：" << SDL_GetError() << '\n';
            return false;
        }

        renderer_ = SDL_CreateRenderer(
            window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            std::cerr << "硬件渲染不可用：" << SDL_GetError()
                      << "，回退到软件渲染。\n";
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer_) {
            std::cerr << "创建渲染器失败：" << SDL_GetError() << '\n';
            return false;
        }

        SDL_RendererInfo rendererInfo = {};
        if (SDL_GetRendererInfo(renderer_, &rendererInfo) == 0) {
            const bool accelerated =
                (rendererInfo.flags & SDL_RENDERER_ACCELERATED) != 0;
            std::cout << "SDL 渲染后端："
                      << (rendererInfo.name ? rendererInfo.name : "unknown")
                      << "（" << (accelerated ? "GPU 硬件渲染" : "CPU 软件渲染")
                      << "）\n";
        } else {
            std::cerr << "无法查询 SDL 渲染器信息：" << SDL_GetError() << '\n';
        }

        texture_ = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
            videoCodec_->width, videoCodec_->height);
        if (!texture_) {
            std::cerr << "创建纹理失败：" << SDL_GetError() << '\n';
            return false;
        }

        return !audioCodec_ || initAudio();
    }

    bool initAudio()
    {
        SDL_AudioSpec wanted = {};
        wanted.freq = audioCodec_->sample_rate > 0 ? audioCodec_->sample_rate : 48000;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 2;
        wanted.samples = 1024;

        audioDevice_ = SDL_OpenAudioDevice(
            nullptr, 0, &wanted, &audioSpec_,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (!audioDevice_) {
            std::cerr << "打开音频设备失败：" << SDL_GetError()
                      << "，将仅播放画面。\n";
            avcodec_free_context(&audioCodec_);
            audioStreamIndex_ = -1;
            return true;
        }

        int64_t inputLayout = audioCodec_->channel_layout;
        if (!inputLayout) {
            inputLayout = av_get_default_channel_layout(audioCodec_->channels);
        }
        const int64_t outputLayout =
            av_get_default_channel_layout(audioSpec_.channels);

        resampler_ = swr_alloc_set_opts(
            nullptr,
            outputLayout, AV_SAMPLE_FMT_S16, audioSpec_.freq,
            inputLayout, audioCodec_->sample_fmt, audioCodec_->sample_rate,
            0, nullptr);
        if (!resampler_ || swr_init(resampler_) < 0) {
            std::cerr << "初始化音频转换器失败，将仅播放画面。\n";
            SDL_CloseAudioDevice(audioDevice_);
            audioDevice_ = 0;
            avcodec_free_context(&audioCodec_);
            audioStreamIndex_ = -1;
            return true;
        }

        audioBytesPerSecond_ =
            audioSpec_.freq * audioSpec_.channels * sizeof(std::int16_t);
        SDL_PauseAudioDevice(audioDevice_, 0);
        return true;
    }

    bool decodePacket(
        AVCodecContext* codec, const AVPacket* packet, bool video)
    {
        if (!codec) {
            return true;
        }

        int result = avcodec_send_packet(codec, packet);
        if (result < 0 && result != AVERROR_EOF) {
            std::cerr << "提交解码数据失败：" << ffError(result) << '\n';
            return true;
        }

        while ((result = avcodec_receive_frame(codec, decodedFrame_)) >= 0) {
            bool running = true;
            if (video) {
                running = presentVideoFrame(decodedFrame_);
            } else {
                running = queueAudioFrame(decodedFrame_);
            }
            av_frame_unref(decodedFrame_);
            if (!running) {
                return false;
            }
        }

        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            std::cerr << "解码失败：" << ffError(result) << '\n';
        }
        return true;
    }

    bool presentVideoFrame(AVFrame* frame)
    {
        bool running = true;
        const int64_t timestamp = frame->best_effort_timestamp;
        if (timestamp != AV_NOPTS_VALUE) {
            const double pts =
                timestamp * av_q2d(format_->streams[videoStreamIndex_]->time_base);
            if (firstVideoPts_ < 0.0) {
                firstVideoPts_ = pts;
                playbackStartTicks_ = SDL_GetTicks();
            }

            const double elapsed =
                static_cast<double>(SDL_GetTicks() - playbackStartTicks_) / 1000.0;
            int waitMs = static_cast<int>((pts - firstVideoPts_ - elapsed) * 1000.0);
            while (running && waitMs > 0) {
                pollEvents(running);
                const int slice = std::min(waitMs, 10);
                SDL_Delay(slice);
                waitMs -= slice;
            }
        }

        if (!running || av_frame_make_writable(displayFrame_) < 0) {
            return running;
        }

        sws_scale(
            scaler_, frame->data, frame->linesize, 0, videoCodec_->height,
            displayFrame_->data, displayFrame_->linesize);

        SDL_UpdateYUVTexture(
            texture_, nullptr,
            displayFrame_->data[0], displayFrame_->linesize[0],
            displayFrame_->data[1], displayFrame_->linesize[1],
            displayFrame_->data[2], displayFrame_->linesize[2]);

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetRendererOutputSize(renderer_, &windowWidth, &windowHeight);
        const double videoAspect =
            static_cast<double>(videoCodec_->width) / videoCodec_->height;
        SDL_Rect target = {0, 0, windowWidth, windowHeight};
        if (static_cast<double>(windowWidth) / windowHeight > videoAspect) {
            target.w = static_cast<int>(windowHeight * videoAspect);
            target.x = (windowWidth - target.w) / 2;
        } else {
            target.h = static_cast<int>(windowWidth / videoAspect);
            target.y = (windowHeight - target.h) / 2;
        }

        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, &target);
        SDL_RenderPresent(renderer_);
        return true;
    }

    bool queueAudioFrame(AVFrame* frame)
    {
        const int maxSamples = static_cast<int>(av_rescale_rnd(
            swr_get_delay(resampler_, audioCodec_->sample_rate) + frame->nb_samples,
            audioSpec_.freq, audioCodec_->sample_rate, AV_ROUND_UP));
        audioBuffer_.resize(
            static_cast<size_t>(maxSamples) *
            audioSpec_.channels * sizeof(std::int16_t));

        uint8_t* output[] = {audioBuffer_.data()};
        const int samples = swr_convert(
            resampler_, output, maxSamples,
            const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (samples < 0) {
            std::cerr << "音频转换失败：" << ffError(samples) << '\n';
            return true;
        }

        const Uint32 bytes = static_cast<Uint32>(
            samples * audioSpec_.channels * sizeof(std::int16_t));
        if (SDL_QueueAudio(audioDevice_, audioBuffer_.data(), bytes) != 0) {
            std::cerr << "音频入队失败：" << SDL_GetError() << '\n';
            return true;
        }

        bool running = true;
        while (running &&
               SDL_GetQueuedAudioSize(audioDevice_) >
                   static_cast<Uint32>(audioBytesPerSecond_)) {
            pollEvents(running);
            SDL_Delay(10);
        }
        return running;
    }

    void pollEvents(bool& running)
    {
        SDL_Event event = {};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }
    }

    void close()
    {
        if (audioDevice_) {
            SDL_ClearQueuedAudio(audioDevice_);
            SDL_CloseAudioDevice(audioDevice_);
        }
        swr_free(&resampler_);
        sws_freeContext(scaler_);
        av_frame_free(&displayFrame_);
        av_frame_free(&decodedFrame_);
        av_packet_free(&packet_);
        avcodec_free_context(&audioCodec_);
        avcodec_free_context(&videoCodec_);
        avformat_close_input(&format_);
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        if (sdlInitialized_) SDL_Quit();
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* videoCodec_ = nullptr;
    AVCodecContext* audioCodec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* decodedFrame_ = nullptr;
    AVFrame* displayFrame_ = nullptr;
    SwsContext* scaler_ = nullptr;
    SwrContext* resampler_ = nullptr;
    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_AudioDeviceID audioDevice_ = 0;
    SDL_AudioSpec audioSpec_ = {};
    bool sdlInitialized_ = false;
    int audioBytesPerSecond_ = 0;
    std::vector<uint8_t> audioBuffer_;

    double firstVideoPts_ = -1.0;
    Uint32 playbackStartTicks_ = 0;
};

} // namespace

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && wcscmp(argv[1], L"--probe") == 0) {
        LocalFree(argv);
        return printFeatureProbe();
    }

    std::string path;
    if (argc > 1) {
        path = wideToUtf8(argv[1]);
    }
    LocalFree(argv);

    if (path.empty()) {
        path = chooseVideoFile();
    }
    if (path.empty()) {
        return 0;
    }

    av_log_set_level(AV_LOG_WARNING);
    VideoPlayer player;
    if (!player.open(path)) {
        MessageBoxA(
            nullptr, "无法打开或播放该视频，详细信息请查看控制台。",
            "FFmpeg 播放 Demo", MB_OK | MB_ICONERROR);
        return 1;
    }
    return player.play();
}
