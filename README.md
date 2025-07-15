《ARM 平台 MP4 播放器（I.MX6ULL 专用）开发与使用》



一、用途和功能



本项目是专为 I.MX6ULL 开发板设计的轻量级 MP4 视频播放器，**核心依赖 FFmpeg 库**（`libavcodec`、`libavformat`、`libavutil`、`libswresample`、`libswscale`）实现视频解析、解码与格式转换，具体特性如下：




*   **硬件适配**：自动适配 RGB565 格式、1024×600 分辨率屏幕，通过 FrameBuffer 设备（`/dev/fb0`）直接渲染视频；


*   **核心功能**：基于 FFmpeg 解析并播放无音频流的 MP4 文件（仅支持 H.264 编码），支持多实例播放（最多 `MAX_PLAYERS 5` 个文件同时播放）；


*   **性能优化**：通过内存对齐（`ALIGNMENT 32`）提升内存访问效率，结合 `libswscale` 实现视频缩放（适配屏幕分辨率）；


*   **接口设计**：提供 `show_mp4_to_lcd` 和 `stop_show_mp4_to_lcd` 接口，封装 FFmpeg 解码、格式转换、LCD 渲染等底层逻辑。


二、用法



### 1. 环境准备&#xA;



*   开发板：I.MX6ULL（配备 1024×600 RGB565 分辨率屏幕）；


*   依赖：项目目录需包含编译好的静态库 `lib_show_mp4_to_lcd.a` 及 FFmpeg-4.4 静态库（`LIB/ffmpeg-4.4`，含 `libavcodec` 等）；


*   视频文件：无音频的 H.264 编码 MP4 文件（如 `demo.mp4`）。


### 2. 核心接口调用&#xA;

#### （1）播放视频&#xA;



```
/\* 播放MP4文件到LCD屏幕（依赖FFmpeg解析和解码）


&#x20;\* 参数:


&#x20;\*   filename - MP4文件路径（无音频H.264编码）


&#x20;\*   duration\_ms - 播放时长(毫秒)，0表示无限播放，>0表示指定时长


&#x20;\* 返回:


&#x20;\*   0 - 成功（启动FFmpeg解码线程）


&#x20;\*   -1 - 失败（文件错误、FFmpeg初始化失败等）


&#x20;\*/


int show\_mp4\_to\_lcd(const char\* filename, int duration\_ms);
```

**示例**：




```
// 无限播放"demo.mp4"


show\_mp4\_to\_lcd("demo.mp4", 0);
```

#### （2）停止播放&#xA;



```
/\* 停止播放指定MP4文件（释放FFmpeg相关资源）


&#x20;\* 参数:


&#x20;\*   filename - 要停止的MP4文件路径


&#x20;\*/


void stop\_show\_mp4\_to\_lcd(const char\* filename);
```

三、编译命令



### 1. 单行编译命令（拆分版）&#xA;

#### （1）生成目标文件与静态库&#xA;



```
arm-linux-gnueabihf-gcc -std=c99 -c LIB/show\_mp4\_to\_lcd/show\_mp4\_to\_lcd.c -o show\_mp4\_to\_lcd.o -I LIB/ffmpeg-4.4 -I LIB/show\_mp4\_to\_lcd && arm-linux-gnueabihf-ar rcs lib\_show\_mp4\_to\_lcd.a show\_mp4\_to\_lcd.o
```

#### （2）链接演示程序&#xA;



```
arm-linux-gnueabihf-gcc demo.c -o demo -L. -l\_show\_mp4\_to\_lcd -I LIB/ffmpeg-4.4 -I LIB/show\_mp4\_to\_lcd \\


-L LIB/ffmpeg-4.4/libavformat -L LIB/ffmpeg-4.4/libavcodec -L LIB/ffmpeg-4.4/libavutil -L LIB/ffmpeg-4.4/libswscale -L LIB/ffmpeg-4.4/libswresample \\


-lavformat -lavcodec -lavutil -lswscale -lswresample -lpthread -lm
```

### 2. Makefile 语句&#xA;



```
CC = arm-linux-gnueabihf-gcc


AR = arm-linux-gnueabihf-ar


CFLAGS = -std=c99 -Wall -I LIB/ffmpeg-4.4 -I LIB/show\_mp4\_to\_lcd -DTARGET\_WIDTH=1024 -DTARGET\_HEIGHT=600


LDFLAGS = -L. -l\_show\_mp4\_to\_lcd \\


&#x20;         -L LIB/ffmpeg-4.4/libavformat \\


&#x20;         -L LIB/ffmpeg-4.4/libavcodec \\


&#x20;         -L LIB/ffmpeg-4.4/libavutil \\


&#x20;         -L LIB/ffmpeg-4.4/libswscale \\


&#x20;         -L LIB/ffmpeg-4.4/libswresample \\


&#x20;         -lavformat -lavcodec -lavutil -lswscale -lswresample \\


&#x20;         -lpthread -lm


SRC\_DIR = LIB/show\_mp4\_to\_lcd


SRC = \$(SRC\_DIR)/show\_mp4\_to\_lcd.c


OBJ = show\_mp4\_to\_lcd.o


LIB = lib\_show\_mp4\_to\_lcd.a


TARGET = demo


all: \$(TARGET)


\$(LIB): \$(OBJ)


&#x20;       \$(AR) rcs \$@ \$^


\$(OBJ): \$(SRC)


&#x20;       \$(CC) \$(CFLAGS) -c \$< -o \$@


\$(TARGET): demo.c \$(LIB)


&#x20;       \$(CC) \$(CFLAGS) \$< -o \$@ \$(LDFLAGS)


clean:


&#x20;       rm -f \$(OBJ) \$(LIB) \$(TARGET)
```

四、屏幕参数获取与 MP4 播放核心流程



### 1. 开发阶段：终端命令确认屏幕信息&#xA;



*   **查看帧缓冲设备**：




```
ls /dev/fb\*  # 预期输出：/dev/fb0
```



*   **获取屏幕详细参数**：




```
fbset -i /dev/fb0  # 需包含"geometry 1024 600"和"bits\_per\_pixel 16"
```

### 2. 代码阶段：动态获取与校验屏幕参数&#xA;



```
void \_init\_lcd\_system() {


&#x20;   int fd = open(FB\_DEVICE, O\_RDWR);  // FB\_DEVICE = "/dev/fb0"


&#x20;   if (fd == -1) {


&#x20;       perror("Failed to open framebuffer device");


&#x20;       return;


&#x20;   }


&#x20;   struct fb\_var\_screeninfo vinfo;


&#x20;   if (ioctl(fd, FBIOGET\_VSCREENINFO, \&vinfo) == -1) {


&#x20;       perror("Failed to get fb info");


&#x20;       close(fd);


&#x20;       return;


&#x20;   }


&#x20;   // 校验分辨率和像素格式


&#x20;   if (vinfo.xres != TARGET\_WIDTH || vinfo.yres != TARGET\_HEIGHT) {


&#x20;       fprintf(stderr, "屏幕分辨率错误（需1024×600）\n");


&#x20;   }


&#x20;   if (vinfo.bits\_per\_pixel != 16) {


&#x20;       fprintf(stderr, "像素格式错误（需RGB565-16位）\n");


&#x20;   }


&#x20;   close(fd);


}
```

### 3. MP4 播放核心代码流程&#xA;

#### （1）初始化与文件打开&#xA;



```
int show\_mp4\_to\_lcd(const char\* filename, int duration\_ms) {


&#x20;   \_init\_lcd\_system();  // 初始化LCD


&#x20;   pthread\_mutex\_lock(\&mutex);


&#x20;   // 查找空闲播放实例（最多5个）


&#x20;   int player\_idx = -1;


&#x20;   for (int i = 0; i < MAX\_PLAYERS; i++) {


&#x20;       if (!players\[i].is\_playing) { player\_idx = i; break; }


&#x20;   }


&#x20;   if (player\_idx == -1) { pthread\_mutex\_unlock(\&mutex); return -1; }


&#x20;   PlayerContext\* ctx = \&players\[player\_idx];


&#x20;   av\_register\_all();  // 初始化FFmpeg


&#x20;   // 打开并解析MP4文件（依赖libavformat）


&#x20;   if (avformat\_open\_input(\&ctx->fmt\_ctx, filename, NULL, NULL) != 0) {


&#x20;       pthread\_mutex\_unlock(\&mutex); return -1;


&#x20;   }


&#x20;   // 查找视频流（依赖libavformat）


&#x20;   ctx->video\_stream\_index = -1;


&#x20;   for (int i = 0; i < ctx->fmt\_ctx->nb\_streams; i++) {


&#x20;       if (ctx->fmt\_ctx->streams\[i]->codecpar->codec\_type == AVMEDIA\_TYPE\_VIDEO) {


&#x20;           ctx->video\_stream\_index = i; break;


&#x20;       }


&#x20;   }


&#x20;   // 初始化解码器（依赖libavcodec）


&#x20;   AVCodecParameters\* codec\_params = ctx->fmt\_ctx->streams\[ctx->video\_stream\_index]->codecpar;


&#x20;   AVCodec\* codec = avcodec\_find\_decoder(codec\_params->codec\_id);


&#x20;   ctx->codec\_ctx = avcodec\_alloc\_context3(codec);


&#x20;   avcodec\_parameters\_to\_context(ctx->codec\_ctx, codec\_params);


&#x20;   avcodec\_open2(ctx->codec\_ctx, codec, NULL);


&#x20;   // 启动解码线程


&#x20;   ctx->is\_playing = 1;


&#x20;   pthread\_create(\&ctx->thread\_id, NULL, decode\_thread, ctx);


&#x20;   pthread\_mutex\_unlock(\&mutex);


&#x20;   return 0;


}
```

#### （2）解码与渲染循环&#xA;



```
static void\* decode\_thread(void\* arg) {


&#x20;   PlayerContext\* ctx = (PlayerContext\*)arg;


&#x20;   while (ctx->is\_playing) {


&#x20;       // 读取视频包（依赖libavformat）


&#x20;       if (av\_read\_frame(ctx->fmt\_ctx, \&ctx->packet) < 0) break;


&#x20;       // 解码视频帧（依赖libavcodec）


&#x20;       if (ctx->packet.stream\_index == ctx->video\_stream\_index) {


&#x20;           avcodec\_send\_packet(ctx->codec\_ctx, \&ctx->packet);


&#x20;           if (avcodec\_receive\_frame(ctx->codec\_ctx, ctx->frame) == 0) {


&#x20;               // 格式转换（YUV420P→RGB565，结合libswscale优化）


&#x20;               yuv420p\_to\_rgb565\_manual(ctx);


&#x20;               // 渲染至LCD


&#x20;               lcd\_write\_rgb565(ctx->rgb\_buffer, ctx->buffer\_width, ctx->buffer\_height, ctx->swap\_rb);


&#x20;           }


&#x20;       }


&#x20;       av\_packet\_unref(\&ctx->packet);


&#x20;   }


&#x20;   return NULL;


}
```

五、在 x86 PC 上部署 ARM 架构的 FFmpeg-4.4 及库链接说明



### 1. 安装预编译的 ARM 版 FFmpeg&#xA;



```
cd show\_mp4\_to\_lcd


tar -zxvf ZIP/ffmpeg-4.4\_for\_armhf.tar.gz -C LIB/  # 生成LIB/ffmpeg-4.4目录
```

### 2. 项目依赖的 FFmpeg 库&#xA;



| 库文件&#xA;          | 功能&#xA;              |
| ----------------- | -------------------- |
| `libavformat.a`   | MP4 格式解析与解复用&#xA;    |
| `libavcodec.a`    | H.264 视频解码&#xA;      |
| `libavutil.a`     | 基础工具（内存管理、数据结构）&#xA; |
| `libswscale.a`    | 视频缩放与像素格式转换&#xA;     |
| `libswresample.a` | 音频重采样（处理内部依赖）&#xA;   |

### 3. FFmpeg 编译开关配置（预编译库已优化）&#xA;



```
./configure \\


&#x20; \--arch=arm \\


&#x20; \--target-os=linux \\


&#x20; \--enable-cross-compile \\


&#x20; \--cross-prefix=arm-linux-gnueabihf- \  # 交叉编译工具前缀


&#x20; \--disable-shared --enable-static \      # 生成静态库


&#x20; \--disable-everything \                  # 禁用所有默认功能


&#x20; \--enable-decoder=h264 \                 # 仅启用H.264解码器


&#x20; \--enable-demuxer=mp4 \                  # 仅支持MP4格式


&#x20; \--enable-protocol=file \                # 仅读取本地文件


&#x20; \--enable-swscale \                      # 启用视频缩放（libswscale）


&#x20; \--enable-swresample \                   # 启用音频重采样（libswresample）


&#x20; \--disable-audio \                       # 禁用音频功能


&#x20; \--disable-encoders \                    # 禁用编码器


&#x20; \--enable-small \                        # 减小库体积


&#x20; \--extra-cflags="-march=armv7-a -mfpu=neon"  # 适配I.MX6ULL架构
```

### 4. 库链接方式&#xA;



```
\# 指定库路径


-L LIB/ffmpeg-4.4/libavformat -L LIB/ffmpeg-4.4/libavcodec \\


-L LIB/ffmpeg-4.4/libavutil -L LIB/ffmpeg-4.4/libswscale -L LIB/ffmpeg-4.4/libswresample


\# 链接库文件


-lavformat -lavcodec -lavutil -lswscale -lswresample
```

六、项目目录说明





```
show\_mp4\_to\_lcd/


├── LIB/


│   ├── ffmpeg-4.4/          # ARM版FFmpeg静态库（含上述5个库）


│   └── show\_mp4\_to\_lcd/     # 核心源码


│       ├── show\_mp4\_to\_lcd.c  # 实现文件


│       └── show\_mp4\_to\_lcd.h  # 接口声明


├── ZIP/


│   └── ffmpeg-4.4\_for\_armhf.tar.gz  # FFmpeg预编译包


├── demo.c                   # 演示程序


├── Makefile                 # 编译脚本


├── show\_mp4\_to\_lcd.o        # 目标文件


├── lib\_show\_mp4\_to\_lcd.a    # 静态库


├── demo                     # 可执行文件


└── demo.mp4                 # 测试视频
```

七、开发过程中的问题及解决方案





1.  **头文件找不到**：添加 `-I LIB/show_mp4_to_lcd` 指定核心头文件路径。


2.  **FFmpeg 库链接失败**：确保 `-L` 指定所有 FFmpeg 库目录，`-l` 包含 5 个依赖库。


3.  **视频花屏**：通过 `check_endianness` 函数适配系统字节序，调整 `swap_rb` 标志。


4.  **多实例播放失败**：减少并发数或修改 `MAX_PLAYERS` 宏后重新编译。


5.  **资源泄漏**：播放结束后必须调用 `stop_show_mp4_to_lcd` 释放 FFmpeg 资源。


> （注：文档部分内容可能由 AI 生成）
>
