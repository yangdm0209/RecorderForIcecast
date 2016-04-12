LOCAL_PATH := $(call my-dir)
 
include $(CLEAR_VARS)
 
LOCAL_MODULE    	:= libicesshout
APP_PLATFORM := android-21
LOCAL_CFLAGS    := -DHAVE_CONFIG_H=1 -ffast-math -fsigned-char -pthread -O2 -ffast-math -Werror -DHAVE_CUALQUIERVARIABLE=1 -I$(LOCAL_PATH)/libvorbis/include -I$(LOCAL_PATH)/libogg/include -I$(LOCAL_PATH)/libshout/include -I$(LOCAL_PATH)/libshout/src
LOCAL_SRC_FILES 	:= \
./libmp3lame/bitstream.c \
./libmp3lame/encoder.c \
./libmp3lame/fft.c \
./libmp3lame/gain_analysis.c \
./libmp3lame/id3tag.c \
./libmp3lame/lame.c \
./libmp3lame/mpglib_interface.c \
./libmp3lame/newmdct.c \
./libmp3lame/presets.c \
./libmp3lame/psymodel.c \
./libmp3lame/quantize.c \
./libmp3lame/quantize_pvt.c \
./libmp3lame/reservoir.c \
./libmp3lame/set_get.c \
./libmp3lame/tables.c \
./libmp3lame/takehiro.c \
./libmp3lame/util.c \
./libmp3lame/vbrquantize.c \
./libmp3lame/VbrTag.c \
./libmp3lame/version.c \
./libvorbis/src/analysis.c \
./libvorbis/src/codebook.c \
./libvorbis/src/floor1.c \
./libvorbis/src/lpc.c \
./libvorbis/src/mdct.c \
./libvorbis/src/res0.c \
./libvorbis/src/synthesis.c \
./libvorbis/src/window.c \
./libvorbis/src/bitrate.c \
./libvorbis/src/envelope.c \
./libvorbis/src/info.c \
./libvorbis/src/lsp.c \
./libvorbis/src/psy.c \
./libvorbis/src/sharedbook.c \
./libvorbis/src/vorbisenc.c \
./libvorbis/src/block.c \
./libvorbis/src/floor0.c \
./libvorbis/src/lookup.c \
./libvorbis/src/mapping0.c \
./libvorbis/src/registry.c \
./libvorbis/src/smallft.c \
./libvorbis/src/vorbisfile.c \
./libogg/src/bitwise.c \
./libogg/src/framing.c \
./libshout/src/shout.c \
./libshout/src/timing \
./libshout/src/mp3.c \
./libshout/src/ogg.c \
./libshout/src/util.c \
./libshout/src/vorbis.c \
./libshout/src/avl/avl.c \
./libshout/src/httpp/httpp.c \
./libshout/src/net/resolver.c \
./libshout/src/net/sock.c \
./libshout/src/thread/thread.c \
./libshout/src/timing/timing.c \
./ringbuf.c \
./record.c

LOCAL_LDLIBS    := -lm -llog -ljnigraphics

include $(BUILD_SHARED_LIBRARY)
