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