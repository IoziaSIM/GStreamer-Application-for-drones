#!/bin/bash
./gstreamer/gst-build/build/subprojects/gstreamer/tools/gst-launch-1.0 -e \
    udpsrc port=6000 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! \
        rtpjitterbuffer ! rtpstorage ! rtph264depay ! h264parse ! avdec_h264 ! \
            videoconvert ! videoscale ! video/x-raw, width=640, height=480 ! \
                ximagesink sync=false \