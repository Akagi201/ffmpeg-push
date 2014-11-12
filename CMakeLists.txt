cmake_minimum_required(VERSION 2.8.4)
project(ffmpeg_push)

set(SOURCE_FILES main.c)

include_directories("/usr/local/include")

link_directories("/usr/local/lib")

add_executable(ffmpeg_push ${SOURCE_FILES})

target_link_libraries(ffmpeg_push "libavformat.a")

target_link_libraries(ffmpeg_push "libavutil.a")

target_link_libraries(ffmpeg_push "libavcodec.a")
