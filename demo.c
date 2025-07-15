#include "show_mp4_to_lcd.h"
#include <stdio.h>

int main() {
    printf("开始播放demo.mp4...\n");
    if (show_mp4_to_lcd("demo.mp4", 0) != 0) {
        printf("播放失败\n");
        return 1;
    }

    printf("按回车键停止播放...\n");
    getchar();
    
    stop_show_mp4_to_lcd("demo.mp4");
    printf("播放已停止\n");
    return 0;
}