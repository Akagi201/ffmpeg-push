ffmpeg-push
===========

Push a file or stream to a stream server based on FFmpeg

## Features
* Can read a file or rtsp/ts stream as input.
* Use cmake for cross-platform build.

## Dependencies
* libavformat
* libavcodec
* libavutil

## Build
* `./start_build.sh`
* build target is located in build/ffmpeg_push

## Usage
* `ffmpeg_push input.{ext}/input_url output_url`