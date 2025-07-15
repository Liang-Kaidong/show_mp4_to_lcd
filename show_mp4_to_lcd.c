#include "show_mp4_to_lcd.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#define MAX_PLAYERS 5
#define TARGET_WIDTH 1024
#define TARGET_HEIGHT 600
#define FB_DEVICE "/dev/fb0"
#define ALIGNMENT 32  // 内存对齐字节数

typedef struct {
    char filename[256];
    AVFormatContext* fmt_ctx;
    AVCodecContext* codec_ctx;
    AVFrame* frame;
    AVPacket packet;
    int is_playing;
    pthread_t thread_id;
    int video_stream_index;
    uint16_t* rgb_buffer;
    int buffer_width;
    int buffer_height;
    int actual_buffer_width;  // 对齐后的缓冲区宽度
    int bytes_per_pixel;      // 每像素字节数
    int swap_rb;              // 是否需要交换红蓝通道
} PlayerContext;

static PlayerContext players[MAX_PLAYERS] = {0};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* 检测系统字节序 */
static int check_endianness() {
    union {
        uint32_t i;
        char c[4];
    } u = {0x01020304};
    
    return u.c[0] == 1 ? 1 : 0;  // 1为大端，0为小端
}

/* YUV420P到RGB565的转换函数（手动实现） */
static void yuv420p_to_rgb565_manual(PlayerContext* ctx) {
    if (!ctx->rgb_buffer) {
        return;
    }
    
    uint8_t* y_plane = ctx->frame->data[0];
    uint8_t* u_plane = ctx->frame->data[1];
    uint8_t* v_plane = ctx->frame->data[2];
    
    int y_linesize = ctx->frame->linesize[0];
    int u_linesize = ctx->frame->linesize[1];
    int v_linesize = ctx->frame->linesize[2];
    
    int src_width = ctx->frame->width;
    int src_height = ctx->frame->height;
    int dst_width = ctx->buffer_width;
    int dst_height = ctx->buffer_height;
    
    /* 简单的双线性缩放和YUV到RGB565转换 */
    for (int y = 0; y < dst_height; y++) {
        int src_y = (y * src_height) / dst_height;
        if (src_y >= src_height) src_y = src_height - 1;
        
        for (int x = 0; x < dst_width; x++) {
            int src_x = (x * src_width) / dst_width;
            if (src_x >= src_width) src_x = src_width - 1;
            
            uint8_t y_val = y_plane[src_y * y_linesize + src_x];
            uint8_t u_val = u_plane[(src_y / 2) * u_linesize + (src_x / 2)];
            uint8_t v_val = v_plane[(src_y / 2) * v_linesize + (src_x / 2)];
            
            /* 转换为RGB */
            int r = y_val + 1.402 * (v_val - 128);
            int g = y_val - 0.344136 * (u_val - 128) - 0.714136 * (v_val - 128);
            int b = y_val + 1.772 * (u_val - 128);
            
            /* 限制范围 */
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            
            /* 转换为RGB565格式 */
            uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            
            /* 根据字节序调整 */
            if (ctx->swap_rb) {
                rgb565 = ((rgb565 & 0xF800) >> 11) | 
                        (rgb565 & 0x07E0) | 
                        ((rgb565 & 0x001F) << 11);
            }
            
            /* 写入目标缓冲区 */
            ctx->rgb_buffer[y * dst_width + x] = rgb565;
        }
    }
}

/* 初始化LCD系统 */
void _init_lcd_system() {
    int fd = open(FB_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("Failed to open framebuffer device during initialization");
        return;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Failed to get variable screen info during initialization");
        close(fd);
        return;
    }
    
    close(fd);
}

/* 实际的LCD写入函数 */
static void lcd_write_rgb565(uint16_t* buffer, int width, int height, int swap_rb) {
    int fd = open(FB_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("Failed to open framebuffer");
        return;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Failed to get fb info");
        close(fd);
        return;
    }
    
    size_t screensize = finfo.line_length * vinfo.yres;
    uint16_t* fb_mem = (uint16_t*)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("Failed to mmap fb");
        close(fd);
        return;
    }

    /* 写入帧缓冲 */
    int fb_pixels_per_line = finfo.line_length / 2;
    
    for (int y = 0; y < vinfo.yres; y++) {
        uint16_t* src_line = (y < height) ? (buffer + y * width) : NULL;
        uint16_t* dst_line = fb_mem + y * fb_pixels_per_line;
        
        if (src_line) {
            for (int x = 0; x < vinfo.xres; x++) {
                if (x < width) {
                    dst_line[x] = src_line[x];
                } else {
                    dst_line[x] = 0x0000;  // 超出部分填充黑色
                }
            }
        } else {
            memset(dst_line, 0, fb_pixels_per_line * 2);  // 超出高度部分填充黑色
        }
    }
    
    munmap(fb_mem, screensize);
    close(fd);
}

static void* decode_thread(void* arg) {
    PlayerContext* ctx = (PlayerContext*)arg;
    
    /* 检测字节序 */
    ctx->swap_rb = check_endianness();
    
    while (ctx->is_playing) {
        if (av_read_frame(ctx->fmt_ctx, &ctx->packet) >= 0) {
            if (ctx->packet.stream_index == ctx->video_stream_index) {
                if (avcodec_send_packet(ctx->codec_ctx, &ctx->packet) == 0) {
                    if (avcodec_receive_frame(ctx->codec_ctx, ctx->frame) == 0) {
                        /* 使用手动转换函数 */
                        yuv420p_to_rgb565_manual(ctx);
                        lcd_write_rgb565(ctx->rgb_buffer, ctx->buffer_width, ctx->buffer_height, ctx->swap_rb);
                    }
                }
                av_packet_unref(&ctx->packet);
            }
        } else {
            break;
        }
    }
    return NULL;
}

int show_mp4_to_lcd(const char* filename, int duration_ms) {
    _init_lcd_system();
    
    pthread_mutex_lock(&mutex);
    int player_idx = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].is_playing) {
            player_idx = i;
            break;
        }
    }
    if (player_idx == -1) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    PlayerContext* ctx = &players[player_idx];
    av_register_all();  // 保留旧版兼容
    
    if (avformat_open_input(&ctx->fmt_ctx, filename, NULL, NULL) != 0) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    if (avformat_find_stream_info(ctx->fmt_ctx, NULL) < 0) {
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    /* 查找视频流 */
    ctx->video_stream_index = -1;
    for (int i = 0; i < ctx->fmt_ctx->nb_streams; i++) {
        if (ctx->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ctx->video_stream_index = i;
            break;
        }
    }
    if (ctx->video_stream_index == -1) {
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    /* 初始化解码器 */
    AVCodecParameters* codec_params = ctx->fmt_ctx->streams[ctx->video_stream_index]->codecpar;
    AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    ctx->codec_ctx = avcodec_alloc_context3(codec);
    if (!ctx->codec_ctx || avcodec_parameters_to_context(ctx->codec_ctx, codec_params) < 0 ||
        avcodec_open2(ctx->codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx->codec_ctx);
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    ctx->frame = av_frame_alloc();
    if (!ctx->frame) {
        avcodec_free_context(&ctx->codec_ctx);
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    /* 分配RGB缓冲区 */
    ctx->buffer_width = TARGET_WIDTH;
    ctx->buffer_height = TARGET_HEIGHT;
    ctx->bytes_per_pixel = 2;  // RGB565
    ctx->actual_buffer_width = ((ctx->buffer_width * ctx->bytes_per_pixel + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT / ctx->bytes_per_pixel;
    ctx->rgb_buffer = (uint16_t*)av_malloc(ctx->actual_buffer_width * ctx->buffer_height * ctx->bytes_per_pixel);
    if (!ctx->rgb_buffer) {
        av_frame_free(&ctx->frame);
        avcodec_free_context(&ctx->codec_ctx);
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    /* 初始化其他参数 */
    strncpy(ctx->filename, filename, sizeof(ctx->filename)-1);
    ctx->is_playing = 1;

    if (pthread_create(&ctx->thread_id, NULL, decode_thread, ctx) != 0) {
        av_free(ctx->rgb_buffer);
        av_frame_free(&ctx->frame);
        avcodec_free_context(&ctx->codec_ctx);
        avformat_close_input(&ctx->fmt_ctx);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    pthread_mutex_unlock(&mutex);
    return 0;
}

void stop_show_mp4_to_lcd(const char* filename) {
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_playing && strcmp(players[i].filename, filename) == 0) {
            players[i].is_playing = 0;
            pthread_join(players[i].thread_id, NULL);
            if (players[i].rgb_buffer) av_free(players[i].rgb_buffer);
            av_frame_free(&players[i].frame);
            avcodec_free_context(&players[i].codec_ctx);
            avformat_close_input(&players[i].fmt_ctx);
            memset(&players[i], 0, sizeof(PlayerContext));
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
}