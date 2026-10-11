// Captures (2026-10-11, AI-assisted): WebM -> MP4 on the console - see ProsperoConvert.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProsperoConvert.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <pthread.h>

// FFmpeg's and x264's worker threads are made with the default attributes, and a payload's default stack is far too
// small for a video encoder (the helper died a second into the first clip): every thread made without a size gets
// 2 MB (the link wraps pthread_create: -Wl,--wrap=pthread_create)
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
extern "C" int __wrap_pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg)
{
	if (attr)
		return __real_pthread_create(t, attr, fn, arg);
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setstacksize(&a, 2 << 20);
	const int r = __real_pthread_create(t, &a, fn, arg);
	pthread_attr_destroy(&a);
	return r;
}

namespace captures
{
namespace
{
std::string AvError(int e)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(e, buf, sizeof(buf));
	return buf;
}

struct Job
{
	AVFormatContext* in = nullptr;
	AVFormatContext* out = nullptr;
	AVCodecContext* vdec = nullptr;
	AVCodecContext* venc = nullptr;
	AVCodecContext* adec = nullptr;
	AVCodecContext* aenc = nullptr;
	SwsContext* sws = nullptr;
	SwrContext* swr = nullptr;
	AVAudioFifo* fifo = nullptr;
	AVFrame* frame = nullptr;
	AVFrame* conv = nullptr;   // the picture for the encoder (8-bit 4:2:0, cropped)
	AVFrame* aframe = nullptr; // a 1024-sample frame for the AAC encoder
	AVPacket* pkt = nullptr;
	int vin = -1, ain = -1, vout = -1, aout = -1;
	int out_w = 0, out_h = 0;
	int64_t next_apts = -1; // the AAC frames' timestamps, in samples (set by the first sound)
	int64_t start = 0;      // the clip's first timestamp (AV_TIME_BASE): the PS5's WebMs start at ~55 s, the MP4 at 0
	bool opened_out = false;

	~Job()
	{
		if (opened_out && out && !(out->oformat->flags & AVFMT_NOFILE))
			avio_closep(&out->pb);
		avformat_free_context(out);
		avformat_close_input(&in);
		avcodec_free_context(&vdec);
		avcodec_free_context(&venc);
		avcodec_free_context(&adec);
		avcodec_free_context(&aenc);
		sws_freeContext(sws);
		swr_free(&swr);
		if (fifo)
			av_audio_fifo_free(fifo);
		av_frame_free(&frame);
		av_frame_free(&conv);
		av_frame_free(&aframe);
		av_packet_free(&pkt);
	}
};

// Sends one encoder's packets to the file.
int Drain(Job& j, AVCodecContext* enc, int stream)
{
	for (;;)
	{
		int r = avcodec_receive_packet(enc, j.pkt);
		if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return 0;
		if (r < 0)
			return r;
		av_packet_rescale_ts(j.pkt, enc->time_base, j.out->streams[stream]->time_base);
		j.pkt->stream_index = stream;
		r = av_interleaved_write_frame(j.out, j.pkt);
		if (r < 0)
			return r;
	}
}

// A decoded picture -> 8-bit 4:2:0 at the output size (the PS5 records 1920x1088: the bottom 8 rows are cut, as the
// clips' 1080p) -> the H.264 encoder.
int EncodeVideo(Job& j, AVFrame* f)
{
	if (f)
	{
		if (!j.sws || f->format != j.vdec->pix_fmt)
		{
			sws_freeContext(j.sws);
			j.sws = sws_getContext(f->width, j.out_h, AVPixelFormat(f->format), j.out_w, j.out_h, AV_PIX_FMT_YUV420P,
				SWS_POINT, nullptr, nullptr, nullptr);
			if (!j.sws)
				return AVERROR(EINVAL);
		}
		int r = av_frame_make_writable(j.conv);
		if (r < 0)
			return r;
		sws_scale(j.sws, f->data, f->linesize, 0, j.out_h, j.conv->data, j.conv->linesize);
		const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
		j.conv->pts = av_rescale_q(ts, j.in->streams[j.vin]->time_base, j.venc->time_base) -
			av_rescale_q(j.start, AV_TIME_BASE_Q, j.venc->time_base);
	}
	const int r = avcodec_send_frame(j.venc, f ? j.conv : nullptr);
	if (r < 0 && r != AVERROR_EOF)
		return r;
	return Drain(j, j.venc, j.vout);
}

// Decoded audio -> 48 kHz stereo float planar (the AAC encoder's) -> 1024-sample frames -> AAC.
int EncodeAudio(Job& j, AVFrame* f, bool flush)
{
	if (f && j.next_apts < 0)
	{
		// the first sound: where it starts against the picture (both from the clip's first timestamp)
		const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
		j.next_apts = ts == AV_NOPTS_VALUE ? 0 : std::max<int64_t>(0, av_rescale_q(ts, j.in->streams[j.ain]->time_base, {1, 48000}) -
			av_rescale_q(j.start, AV_TIME_BASE_Q, {1, 48000}));
	}
	if (f)
	{
		uint8_t** buf = nullptr;
		const int max_out = swr_get_out_samples(j.swr, f->nb_samples);
		int r = av_samples_alloc_array_and_samples(&buf, nullptr, 2, max_out, AV_SAMPLE_FMT_FLTP, 0);
		if (r < 0)
			return r;
		const int got = swr_convert(j.swr, buf, max_out, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
		if (got > 0)
			av_audio_fifo_write(j.fifo, reinterpret_cast<void**>(buf), got);
		av_freep(&buf[0]);
		av_freep(&buf);
		if (got < 0)
			return got;
	}
	const int fs = j.aenc->frame_size;
	while (av_audio_fifo_size(j.fifo) >= fs || (flush && av_audio_fifo_size(j.fifo) > 0))
	{
		const int n = std::min(fs, av_audio_fifo_size(j.fifo));
		int r = av_frame_make_writable(j.aframe);
		if (r < 0)
			return r;
		j.aframe->nb_samples = n;
		av_audio_fifo_read(j.fifo, reinterpret_cast<void**>(j.aframe->data), n);
		if (j.next_apts < 0)
			j.next_apts = 0;
		j.aframe->pts = j.next_apts;
		j.next_apts += n;
		r = avcodec_send_frame(j.aenc, j.aframe);
		if (r < 0)
			return r;
		r = Drain(j, j.aenc, j.aout);
		if (r < 0)
			return r;
	}
	if (flush)
	{
		avcodec_send_frame(j.aenc, nullptr);
		return Drain(j, j.aenc, j.aout);
	}
	return 0;
}

bool Fail(std::string* error, const std::string& what, int e = 0)
{
	if (error)
		*error = e ? what + ": " + AvError(e) : what;
	return false;
}
} // namespace

bool ConvertToMp4(const std::string& in_path, const std::string& out_path, std::atomic<int>* percent,
	const std::atomic<bool>* stop, std::string* error)
{
	Job j;
	int r = avformat_open_input(&j.in, in_path.c_str(), nullptr, nullptr);
	if (r < 0)
		return Fail(error, "open " + in_path, r);
	if ((r = avformat_find_stream_info(j.in, nullptr)) < 0)
		return Fail(error, "stream info", r);
	j.vin = av_find_best_stream(j.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	j.ain = av_find_best_stream(j.in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (j.vin < 0)
		return Fail(error, "no video in the clip");
	const double duration = j.in->duration > 0 ? j.in->duration / double(AV_TIME_BASE) : 0;
	j.start = j.in->start_time != AV_NOPTS_VALUE ? j.in->start_time : 0;

	// decoders
	auto open_decoder = [&](int si, AVCodecContext** ctx, int threads) -> int {
		const AVStream* st = j.in->streams[si];
		const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
		if (!c)
			return AVERROR_DECODER_NOT_FOUND;
		*ctx = avcodec_alloc_context3(c);
		avcodec_parameters_to_context(*ctx, st->codecpar);
		(*ctx)->pkt_timebase = st->time_base;
		(*ctx)->thread_count = threads;
		return avcodec_open2(*ctx, c, nullptr);
	};
	if ((r = open_decoder(j.vin, &j.vdec, 4)) < 0)
		return Fail(error, "video decoder", r);
	if (j.ain >= 0 && open_decoder(j.ain, &j.adec, 1) < 0)
		j.ain = -1; // (no sound rather than no MP4)

	// the output: H.264 (x264) + AAC in an MP4
	if ((r = avformat_alloc_output_context2(&j.out, nullptr, "mp4", out_path.c_str())) < 0)
		return Fail(error, "mp4 muxer", r);
	j.out_w = j.vdec->width & ~1;
	j.out_h = j.vdec->height == 1088 ? 1080 : (j.vdec->height & ~1);
	{
		const AVCodec* c = avcodec_find_encoder_by_name("libx264");
		if (!c)
			return Fail(error, "no H.264 encoder");
		j.venc = avcodec_alloc_context3(c);
		AVRational fps = av_guess_frame_rate(j.in, j.in->streams[j.vin], nullptr);
		if (fps.num <= 0 || fps.den <= 0)
			fps = {60, 1};
		j.venc->width = j.out_w;
		j.venc->height = j.out_h;
		j.venc->pix_fmt = AV_PIX_FMT_YUV420P;
		j.venc->time_base = av_inv_q(fps);
		j.venc->framerate = fps;
		j.venc->gop_size = int(av_q2d(fps) * 2);
		j.venc->thread_count = 8;
		j.venc->color_range = AVCOL_RANGE_MPEG;
		if (j.out->oformat->flags & AVFMT_GLOBALHEADER)
			j.venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		av_opt_set(j.venc->priv_data, "preset", "superfast", 0); // (the phone is waiting for it)
		av_opt_set(j.venc->priv_data, "crf", "21", 0);
		av_opt_set(j.venc->priv_data, "profile", "high", 0);
		if ((r = avcodec_open2(j.venc, c, nullptr)) < 0)
			return Fail(error, "H.264 encoder", r);
		AVStream* st = avformat_new_stream(j.out, nullptr);
		avcodec_parameters_from_context(st->codecpar, j.venc);
		st->time_base = j.venc->time_base;
		j.vout = st->index;
	}
	if (j.ain >= 0)
	{
		const AVCodec* c = avcodec_find_encoder(AV_CODEC_ID_AAC);
		j.aenc = c ? avcodec_alloc_context3(c) : nullptr;
		if (j.aenc)
		{
			j.aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
			j.aenc->sample_rate = 48000;
			av_channel_layout_default(&j.aenc->ch_layout, 2);
			j.aenc->bit_rate = 192000;
			j.aenc->time_base = {1, 48000};
			if (j.out->oformat->flags & AVFMT_GLOBALHEADER)
				j.aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}
		AVChannelLayout in_layout = j.adec->ch_layout;
		if (in_layout.nb_channels == 0)
			av_channel_layout_default(&in_layout, 2);
		if (!j.aenc || avcodec_open2(j.aenc, c, nullptr) < 0 ||
			swr_alloc_set_opts2(&j.swr, &j.aenc->ch_layout, AV_SAMPLE_FMT_FLTP, 48000, &in_layout, j.adec->sample_fmt,
				j.adec->sample_rate, 0, nullptr) < 0 ||
			swr_init(j.swr) < 0)
			return Fail(error, "audio converter");
		j.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, 4096);
		AVStream* st = avformat_new_stream(j.out, nullptr);
		avcodec_parameters_from_context(st->codecpar, j.aenc);
		st->time_base = j.aenc->time_base;
		j.aout = st->index;
		j.aframe = av_frame_alloc();
		j.aframe->format = AV_SAMPLE_FMT_FLTP;
		j.aframe->sample_rate = 48000;
		av_channel_layout_copy(&j.aframe->ch_layout, &j.aenc->ch_layout);
		j.aframe->nb_samples = j.aenc->frame_size;
		if (av_frame_get_buffer(j.aframe, 0) < 0)
			return Fail(error, "audio buffer");
	}
	if ((r = avio_open(&j.out->pb, out_path.c_str(), AVIO_FLAG_WRITE)) < 0)
		return Fail(error, "create " + out_path, r);
	j.opened_out = true;
	AVDictionary* mux_opts = nullptr;
	av_dict_set(&mux_opts, "movflags", "+faststart", 0); // (the index first: phones start playing at once)
	r = avformat_write_header(j.out, &mux_opts);
	av_dict_free(&mux_opts);
	if (r < 0)
		return Fail(error, "mp4 header", r);

	j.frame = av_frame_alloc();
	j.conv = av_frame_alloc();
	j.conv->format = AV_PIX_FMT_YUV420P;
	j.conv->width = j.out_w;
	j.conv->height = j.out_h;
	j.pkt = av_packet_alloc();
	AVPacket* in_pkt = av_packet_alloc();
	if (av_frame_get_buffer(j.conv, 0) < 0 || !in_pkt)
		return Fail(error, "picture buffer");

	auto decode = [&](AVCodecContext* dec, const AVPacket* p, bool video) -> int {
		int e = avcodec_send_packet(dec, p);
		if (e < 0 && e != AVERROR_EOF)
			return e;
		for (;;)
		{
			e = avcodec_receive_frame(dec, j.frame);
			if (e == AVERROR(EAGAIN) || e == AVERROR_EOF)
				return 0;
			if (e < 0)
				return e;
			if (video)
			{
				if (percent && duration > 0 && j.frame->best_effort_timestamp != AV_NOPTS_VALUE)
				{
					const double t = j.frame->best_effort_timestamp * av_q2d(j.in->streams[j.vin]->time_base) - j.start / double(AV_TIME_BASE);
					percent->store(std::max(0, std::min(99, int(t / duration * 100))));
				}
				e = EncodeVideo(j, j.frame);
			}
			else
				e = EncodeAudio(j, j.frame, false);
			av_frame_unref(j.frame);
			if (e < 0)
				return e;
		}
	};
	while ((r = av_read_frame(j.in, in_pkt)) >= 0)
	{
		if (stop && stop->load())
		{
			av_packet_free(&in_pkt);
			return Fail(error, "stopped");
		}
		int e = 0;
		if (in_pkt->stream_index == j.vin)
			e = decode(j.vdec, in_pkt, true);
		else if (in_pkt->stream_index == j.ain)
			e = decode(j.adec, in_pkt, false);
		av_packet_unref(in_pkt);
		if (e < 0)
		{
			av_packet_free(&in_pkt);
			return Fail(error, "converting", e);
		}
	}
	av_packet_free(&in_pkt);
	// the decoders' and encoders' last frames
	if ((r = decode(j.vdec, nullptr, true)) < 0 || (r = EncodeVideo(j, nullptr)) < 0)
		return Fail(error, "finishing the video", r);
	if (j.ain >= 0 && ((r = decode(j.adec, nullptr, false)) < 0 || (r = EncodeAudio(j, nullptr, true)) < 0))
		return Fail(error, "finishing the sound", r);
	if ((r = av_write_trailer(j.out)) < 0)
		return Fail(error, "mp4 index", r);
	if (percent)
		percent->store(100);
	return true;
}
} // namespace captures
