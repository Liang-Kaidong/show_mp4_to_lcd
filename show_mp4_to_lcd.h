/* show_mp4_to_lcd.h */
#ifndef SHOW_MP4_TO_LCD_H
#define SHOW_MP4_TO_LCD_H

/* 播放MP4文件到LCD屏幕
 * 参数:
 *   filename - MP4文件路径
 *   duration_ms - 播放时长(毫秒)，0表示无限播放
 * 返回:
 *   0 - 成功
 *   -1 - 失败
 */
int show_mp4_to_lcd(const char* filename, int duration_ms);

/* 停止播放MP4文件
 * 参数:
 *   filename - 要停止的MP4文件路径
 */
void stop_show_mp4_to_lcd(const char* filename);

#endif /* SHOW_MP4_TO_LCD_H */