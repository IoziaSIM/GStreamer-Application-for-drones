#!/bin/bash
./gstreamer/gst-build/build/subprojects/gstreamer/tools/gst-launch-1.0 -e \
    funnel name=f forward-sticky-events=false ! rtpjitterbuffer ! rtpstorage ! rtph264depay ! h264parse ! avdec_h264 ! \
           videoconvert ! videoscale ! video/x-raw, width=640, height=480 ! ximagesink sync=false \
	udpsrc port=4000 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! \
        .recv_rtp_sink rtpsession name=ses1 probation=0 .recv_rtp_src ! \
            rtpjitterbuffer ! rtpptdemux ! f. \
    udpsrc port=5000 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! \
        .recv_rtp_sink rtpsession name=ses2 probation=0 .recv_rtp_src ! \
            rtpjitterbuffer ! rtpptdemux ! f. \
    udpsrc port=4001 caps="application/x-rtcp" ! ses1.recv_rtcp_sink \
    udpsrc port=5001 caps="application/x-rtcp" ! ses2.recv_rtcp_sink \
    ses1.send_rtcp_src ! udpsink host=127.0.0.1 port=4003 sync=false async=false \
	ses2.send_rtcp_src ! udpsink host=127.0.0.1 port=5003 sync=false async=false 