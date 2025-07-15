# 《ARM 平台 MP4 播放器（I.MX6ULL 专用）开发与使用》


## 一、用途和功能  
本项目是专为 I.MX6ULL 开发板设计的轻量级 MP4 视频播放器，**核心依赖 FFmpeg 库**（`libavcodec`、`libavformat`、`libavutil`、`libswresample`、`libswscale`）实现视频解析、解码与格式转换，具体特性如下：  
- **硬件适配**：自动适配 RGB565 格式、1024×600 分辨率屏幕，通过 FrameBuffer 设备（`/dev/fb0`）直接渲染视频；  
- **核心功能**：基于 FFmpeg 解析并播放无音频流的 MP4 文件（仅支持 H.264 编码），支持多实例播放（最多 `MAX_PLAYERS 5` 个文件同时播放）；  
- **性能优化**：通过内存对齐（`ALIGNMENT 32`）提升内存访问效率，结合 `libswscale` 实现视频缩放（适配屏幕分辨率）；  
- **接口设计**：提供 `show_mp4_to_lcd` 和 `stop_show_mp4_to_lcd` 接口，封装 FFmpeg 解码、格式转换、LCD 渲染等底层逻辑。  


## 二、用法  
### 1. 环境准备  
- 开发板：I.MX6ULL（配备 1024×600 RGB565 分辨率屏幕）；  
- 依赖：项目目录需包含编译好的静态库 `lib_show_mp4_to_lcd.a` 及 FFmpeg-4.4 静态库（`LIB/ffmpeg-4.4`，含 `libavcodec` 等）；  
- 视频文件：无音频的 H.264 编码 MP4 文件（如 `demo.mp4`）。  


### 2. 核心接口调用  
#### （1）播放视频  
```c
/* 播放MP4文件到LCD屏幕（依赖FFmpeg解析和解码）
 * 参数:
 *   filename - MP4文件路径（无音频H.264编码）
 *   duration_ms - 播放时长(毫秒)，0表示无限播放，>0表示指定时长
 * 返回:
 *   0 - 成功（启动FFmpeg解码线程）
 *   -1 - 失败（文件错误、FFmpeg初始化失败等）
 */
int show_mp4_to_lcd(const char* filename, int duration_ms);
```  

**示例**：  
```c
// 无限播放"demo.mp4"
show_mp4_to_lcd("demo.mp4", 0);
```  


#### （2）停止播放  
```c
/* 停止播放指定MP4文件（释放FFmpeg相关资源）
 * 参数:
 *   filename - 要停止的MP4文件路径
 */
void stop_show_mp4_to_lcd(const char* filename);
```  


## 三、编译命令  
### 1. 单行编译命令（拆分版）  
#### （1）生成目标文件与静态库  
```bash
arm-linux-gnueabihf-gcc -std=c99 -c LIB/show_mp4_to_lcd/show_mp4_to_lcd.c -o show_mp4_to_lcd.o -I LIB/ffmpeg-4.4 -I LIB/show_mp4_to_lcd && arm-linux-gnueabihf-ar rcs lib_show_mp4_to_lcd.a show_mp4_to_lcd.o
```  

#### （2）链接演示程序  
```bash
arm-linux-gnueabihf-gcc demo.c -o demo -L. -l_show_mp4_to_lcd -I LIB/ffmpeg-4.4 -I LIB/show_mp4_to_lcd \
-L LIB/ffmpeg-4.4/libavformat -L LIB/ffmpeg-4.4/libavcodec -L LIB/ffmpeg-4.4/libavutil -L LIB/ffmpeg-4.4/libswscale -L LIB/ffmpeg-4.4/libswresample \
-lavformat -lavcodec -lavutil -lswscale -lswresample -lpthread -lm
```  


### 2. Makefile 语句  
```makefile
CC = arm-linux-gnueabihf-gcc
AR = arm-linux-gnueabihf-ar

CFLAGS = -std=c99 -Wall -I LIB/ffmpeg-4.4 -I LIB/show_mp4_to_lcd -DTARGET_WIDTH=1024 -DTARGET_HEIGHT=600
LDFLAGS = -L. -l_show_mp4_to_lcd \
          -L LIB/ffmpeg-4.4/libavformat \
          -L LIB/ffmpeg-4.4/libavcodec \
          -L LIB/ffmpeg-4.4/libavutil \
          -L LIB/ffmpeg-4.4/libswscale \
          -L LIB/ffmpeg-4.4/libswresample \
          -lavformat -lavcodec -lavutil -lswscale -lswresample \
          -lpthread -lm

SRC_DIR = LIB/show_mp4_to_lcd
SRC = $(SRC_DIR)/show_mp4_to_lcd.c
OBJ = show_mp4_to_lcd.o
LIB = lib_show_mp4_to_lcd.a
TARGET = demo

all: $(TARGET)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): demo.c $(LIB)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(LIB) $(TARGET)
```  


## 四、屏幕参数获取与MP4播放核心流程  
### 1. 开发阶段：终端命令确认屏幕信息  
- **查看帧缓冲设备**：  
  ```bash
  ls /dev/fb*  # 预期输出：/dev/fb0
  ```  
- **获取屏幕详细参数**：  
  ```bash
  fbset -i /dev/fb0  # 需包含"geometry 1024 600"和"bits_per_pixel 16"
  ```  


### 2. 代码阶段：动态获取与校验屏幕参数  
```c
void _init_lcd_system() {
    int fd = open(FB_DEVICE, O_RDWR);  // FB_DEVICE = "/dev/fb0"
    if (fd == -1) {
        perror("Failed to open framebuffer device");
        return;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Failed to get fb info");
        close(fd);
        return;
    }

    // 校验分辨率和像素格式
    if (vinfo.xres != TARGET_WIDTH || vinfo.yres != TARGET_HEIGHT) {
        fprintf(stderr, "屏幕分辨率错误（需1024×600）\n");
    }
    if (vinfo.bits_per_pixel != 16) {
        fprintf(stderr, "像素格式错误（需RGB565-16位）\n");
    }

    close(fd);
}
```  


### 3. MP4播放核心代码流程  
#### （1）初始化与文件打开  
```c
int show_mp4_to_lcd(const char* filename, int duration_ms) {
    _init_lcd_system();  // 初始化LCD
    pthread_mutex_lock(&mutex);

    // 查找空闲播放实例（最多5个）
    int player_idx = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].is_playing) { player_idx = i; break; }
    }
    if (player_idx == -1) { pthread_mutex_unlock(&mutex); return -1; }

    PlayerContext* ctx = &players[player_idx];
    av_register_all();  // 初始化FFmpeg

    // 打开并解析MP4文件（依赖libavformat）
    if (avformat_open_input(&ctx->fmt_ctx, filename, NULL, NULL) != 0) {
        pthread_mutex_unlock(&mutex); return -1;
    }

    // 查找视频流（依赖libavformat）
    ctx->video_stream_index = -1;
    for (int i = 0; i < ctx->fmt_ctx->nb_streams; i++) {
        if (ctx->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ctx->video_stream_index = i; break;
        }
    }

    // 初始化解码器（依赖libavcodec）
    AVCodecParameters* codec_params = ctx->fmt_ctx->streams[ctx->video_stream_index]->codecpar;
    AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    ctx->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx->codec_ctx, codec_params);
    avcodec_open2(ctx->codec_ctx, codec, NULL);

    // 启动解码线程
    ctx->is_playing = 1;
    pthread_create(&ctx->thread_id, NULL, decode_thread, ctx);

    pthread_mutex_unlock(&mutex);
    return 0;
}
```  


#### （2）解码与渲染循环  
```c
static void* decode_thread(void* arg) {
    PlayerContext* ctx = (PlayerContext*)arg;

    while (ctx->is_playing) {
        // 读取视频包（依赖libavformat）
        if (av_read_frame(ctx->fmt_ctx, &ctx->packet) < 0) break;

        // 解码视频帧（依赖libavcodec）
        if (ctx->packet.stream_index == ctx->video_stream_index) {
            avcodec_send_packet(ctx->codec_ctx, &ctx->packet);
            if (avcodec_receive_frame(ctx->codec_ctx, ctx->frame) == 0) {
                // 格式转换（YUV420P→RGB565，结合libswscale优化）
                yuv420p_to_rgb565_manual(ctx);
                // 渲染至LCD
                lcd_write_rgb565(ctx->rgb_buffer, ctx->buffer_width, ctx->buffer_height, ctx->swap_rb);
            }
        }
        av_packet_unref(&ctx->packet);
    }
    return NULL;
}
```  


## 五、在 x86 PC 上部署 ARM 架构的 FFmpeg-4.4 及库链接说明  
### 1. 安装预编译的 ARM 版 FFmpeg  
```bash
cd show_mp4_to_lcd
tar -zxvf ZIP/ffmpeg-4.4_for_armhf.tar.gz -C LIB/  # 生成LIB/ffmpeg-4.4目录
```  


### 2. 项目依赖的 FFmpeg 库  
| 库文件          | 功能                          |  
|-----------------|-------------------------------|  
| `libavformat.a`  | MP4格式解析与解复用           |  
| `libavcodec.a`   | H.264视频解码                 |  
| `libavutil.a`    | 基础工具（内存管理、数据结构） |  
| `libswscale.a`   | 视频缩放与像素格式转换        |  
| `libswresample.a`| 音频重采样（处理内部依赖）    |  


### 3. FFmpeg 编译开关配置（预编译库已优化）  
```bash
./configure \
  --arch=arm \
  --target-os=linux \
  --enable-cross-compile \
  --cross-prefix=arm-linux-gnueabihf- \  # 交叉编译工具前缀
  --disable-shared --enable-static \      # 生成静态库
  --disable-everything \                  # 禁用所有默认功能
  --enable-decoder=h264 \                 # 仅启用H.264解码器
  --enable-demuxer=mp4 \                  # 仅支持MP4格式
  --enable-protocol=file \                # 仅读取本地文件
  --enable-swscale \                      # 启用视频缩放（libswscale）
  --enable-swresample \                   # 启用音频重采样（libswresample）
  --disable-audio \                       # 禁用音频功能
  --disable-encoders \                    # 禁用编码器
  --enable-small \                        # 减小库体积
  --extra-cflags="-march=armv7-a -mfpu=neon"  # 适配I.MX6ULL架构
```  


### 4. 编译命令  
```bash
# 配置完成后执行编译（使用4个线程加速）
make -j4
# 安装到指定目录（可选）
make install DESTDIR=./install
```  


### 5. 库链接方式  
```bash
# 指定库路径
-L LIB/ffmpeg-4.4/libavformat -L LIB/ffmpeg-4.4/libavcodec \
-L LIB/ffmpeg-4.4/libavutil -L LIB/ffmpeg-4.4/libswscale -L LIB/ffmpeg-4.4/libswresample

# 链接库文件
-lavformat -lavcodec -lavutil -lswscale -lswresample
```  


## 六、项目目录说明  
```
show_mp4_to_lcd/
├── LIB/
│   ├── ffmpeg-4.4/          # ARM版FFmpeg静态库（含上述5个库）
│   └── show_mp4_to_lcd/     # 核心源码
│       ├── show_mp4_to_lcd.c  # 实现文件
│       └── show_mp4_to_lcd.h  # 接口声明
├── ZIP/
│   └── ffmpeg-4.4_for_armhf.tar.gz  # FFmpeg预编译包
├── demo.c                   # 演示程序
├── Makefile                 # 编译脚本
├── show_mp4_to_lcd.o        # 目标文件
├── lib_show_mp4_to_lcd.a    # 静态库
├── demo                     # 可执行文件
└── demo.mp4                 # 测试视频
```  


## 七、开发过程中的问题及解决方案  
1. **头文件找不到**：添加 `-I LIB/show_mp4_to_lcd` 指定核心头文件路径。  
2. **FFmpeg库链接失败**：确保 `-L` 指定所有FFmpeg库目录，`-l` 包含5个依赖库。  
3. **视频花屏**：通过 `check_endianness` 函数适配系统字节序，调整 `swap_rb` 标志。  
4. **多实例播放失败**：减少并发数或修改 `MAX_PLAYERS` 宏后重新编译。  
5. **资源泄漏**：播放结束后必须调用 `stop_show_mp4_to_lcd` 释放FFmpeg资源。
