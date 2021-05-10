#!/bin/bash
./gstreamer/gst-build/build/subprojects/gstreamer/tools/gst-launch-1.0 \
    v4l2src ! videoconvert ! video/x-raw, format=I420 ! x264enc tune=zerolatency ! \
    h264parse ! rtph264pay config-interval=1 ! \
	 	roundrobin name=t srcpads=3 rate="<0.5, 0.3, 0.2>" repetition="<0.0, 1.0, 0.0>" \
        t.src_0 ! queue ! .send_rtp_sink rtpsession name=ses1 srcpads=3 .send_rtp_src ! udpsink host=127.0.0.1 port=4000 \
		t.src_1 ! queue ! .send_rtp_sink rtpsession name=ses2 srcpads=3 .send_rtp_src ! udpsink host=127.0.0.1 port=5000 \
        t.src_2 ! queue ! .send_rtp_sink rtpsession name=ses3 srcpads=3 .send_rtp_src ! udpsink host=127.0.0.1 port=7000 \
    ses1.send_rtcp_src ! udpsink host=127.0.0.1 port=4001 sync=false async=false \
	ses2.send_rtcp_src ! udpsink host=127.0.0.1 port=5001 sync=false async=false \
    ses3.send_rtcp_src ! udpsink host=127.0.0.1 port=7001 sync=false async=false \
	udpsrc port=4003 caps="application/x-rtcp" ! ses1.recv_rtcp_sink  \
	udpsrc port=5003 caps="application/x-rtcp" ! ses2.recv_rtcp_sink  \
    udpsrc port=7003 caps="application/x-rtcp" ! ses3.recv_rtcp_sink 
	