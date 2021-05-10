#!/usr/bin/python3

import sys
import gi
import os
import socket
gi.require_version('Gst', '1.0')
gi.require_version('GLib', '2.0')
gi.require_version('GObject', '2.0')
gi.require_version("Gtk", '3.0')
from gi.repository import GLib, Gst, Gtk, GObject

PATH_PLOSS = "path/ploss.txt"
UDP_IP = "127.0.0.1"
REM_IP = "127.0.0.1"
UDP_PORT = 12000
channels = 2  #numero di canali
default = False
rate_0 = [0.5, 0.5]  # rate iniziale
rep_0 = [0.0,  1.0]  # repetition iniziale
index = 4  #indice per classe di efficienza
alfa = 1

ploss_0 = [0] * (channels+1)
agg_0 = 0

def bus_call(bus, msg, *args):
    if msg.type == Gst.MessageType.EOS:
        print("End-of-stream")
        loop.quit()
        return
    elif msg.type == Gst.MessageType.ERROR:
        print("GST ERROR", msg.parse_error())
        loop.quit()
        return
    return True

#definizione classi di efficienza
def eff_class(ind):
    global alfa  # modifico il peso di isteresi in base alla classe (dir. prop. a questa)
    #caso 2 canali
    if (channels == 2):
        if (ind == 1):
            alfa = 0
            return [1.0, 0.0]
        elif (ind == 2):
            alfa = 0.25
            return [0.75, 0.25]
        elif (ind == 3):
            alfa = 0.5
            return [0.5, 0.5]
        elif (ind == 4):
            alfa = 1
            return [0.0, 1.0]
    #caso 3 canali
    elif (channels == 3):
        if (ind == 1):
            alfa = 0
            return [1.0, 0.0, 0.0]
        elif (ind == 2):
            alfa = 0.15
            return [0.75, 0.25, 0.0]
        elif (ind == 3):
            alfa = 0.35
            return [0.25, 0.75, 0.0]
        elif (ind == 4):
            alfa = 0.5
            return [0.0, 1.0, 0.0]
        elif (ind == 5):
            alfa = 0.65
            return [0.0, 0.75, 0.25]
        elif (ind == 6):
            alfa = 0.8
            return [0.0, 0.5, 0.5]
        elif (ind == 7):
            alfa = 1
            return [0.0, 0.0, 1.0]

def controller():
    print ("\n Leggo...\n")
    #inizializzazioni
    global index
    global ploss_0
    global agg_0
    ploss_1 = []   # contiene i valori di packet loss dei canali
    sum_loss = 0
    rate = []
    rep = []
    agg_1 = 0

    #lettura dei vari packet loss dei canali
    try:
        with open (PATH_PLOSS, "r") as f:
            txt = f.readlines()
            for value in txt:
                ploss_1.append(int(value))
    except FileNotFoundError:
        print ("File .txt not found!")
        return True

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        data, addr = sock.recvfrom(1024)
        agg_1 = int.from_bytes(data, "little")
        print(agg_1)
    finally:
        sock.close()

    #gestione del repetition
    if (agg_1 > agg_0): #confronto con dato precedente
        if ((channels == 2 and index != 4) or (channels == 3 and index != 7)):
            index += 1    # incremento classe di efficienza
    else:
        if (index != 1):
            index -= 1   # decremento classe di efficienza

    rep = eff_class (index)   # nuovi tassi di repetition

    #gestione del rate
    if ((channels == 2 and index == 4) or (channels == 3 and index == 7)):
        for i in range (channels):
            rate.append(round(1 / channels, 2))  # se siamo in classe limite, metto rate a massima fairness
    else:
        #calcolo sum_loss
        for i in range (channels):
            s = ploss_1[i]-ploss_0[i]  # precisazione per evitare 0^-1
            if(s == 0):
                s = 10**-2
            sum_loss += (s**-1)

        #nuovo rate
        for i in range (channels):
            s = ploss_1[i]-ploss_0[i]
            if (s == 0):
                s = 10**-2
            rt = (s**-1) / sum_loss   # calcolo r(t)
            rate.append (round (alfa * rate_0[i] + (1-alfa) * rt, 2)) # round per arrotondare a 2 decimali


    if (sum(rate) != 1.0):
        i = rate.index(min(rate))
        rate[i] += 0.01

    #salvo i dati per il prossimo ciclo
    ploss_0 = ploss_1.copy()
    agg_0 = agg_1

    #change rate
    print ("\n Setting rate...\n")
    for value in rate:
        rr.set_property("python-rate", value)
        print(value)

    #change repetition
    print ("\n Setting repetition...\n")
    for value in rep:
        rr.set_property("python-repetition", value)
        print(value)

    return True

if __name__ == "__main__":
    # initialization
    loop = GLib.MainLoop()
    Gst.init(None)
    # Create pipeline without name
    pipeline = Gst.Pipeline()
    # Bus for error's messages
    bus = pipeline.get_bus()
    bus.add_watch(0, bus_call, loop)

    #Creation of elements

    #source
    src = Gst.ElementFactory.make('v4l2src', None)
    #filter
    conv = Gst.ElementFactory.make('videoconvert', None)
    caps = Gst.Caps.from_string("video/x-raw, format=I420")
    camerafilter = Gst.ElementFactory.make("capsfilter", None) 
    camerafilter.set_property("caps", caps)
    #H264
    encoder = Gst.ElementFactory.make('x264enc', None)
    encoder.set_property("tune", "zerolatency")
    parse = Gst.ElementFactory.make('h264parse', None)
    #rtp-payloader
    pay = Gst.ElementFactory.make('rtph264pay', None)
    pay.set_property("config-interval", 1)
    #scheduler
    rr = Gst.ElementFactory.make('roundrobin', None)
    rr.set_property("srcpads", channels)
    if(default == False):
        for value in rate_0:
            rr.set_property("python-rate", value)
        for value in rep_0:
            rr.set_property("python-repetition", value)
    rrpad = Gst.Element.get_static_pad(rr, "sink")
    rrpad1 = Gst.Element.get_request_pad(rr, "src_0")
    rrpad2 = Gst.Element.get_request_pad(rr, "src_1")
    rrpad3 = Gst.Element.get_request_pad(rr, "src_2")
    queue1 = Gst.ElementFactory.make('queue', None)
    queue2 = Gst.ElementFactory.make('queue', None)
    queue3 = Gst.ElementFactory.make('queue', None)
    qsrc1 = Gst.Element.get_static_pad(queue1, "src")
    qsrc2 = Gst.Element.get_static_pad(queue2, "src")
    qsrc3 = Gst.Element.get_static_pad(queue3, "src")
    qsink1 = Gst.Element.get_static_pad(queue1, "sink")
    qsink2 = Gst.Element.get_static_pad(queue2, "sink")
    qsink3 = Gst.Element.get_static_pad(queue3, "sink")
    #rtpsession
    rtpsession1 = Gst.ElementFactory.make('rtpsession', None)
    rtpsession2 = Gst.ElementFactory.make('rtpsession', None)
    rtpsession3 = Gst.ElementFactory.make('rtpsession', None)
    rtpsession1.set_property("srcpads", channels)
    rtpsession2.set_property("srcpads", channels)
    rtpsession3.set_property("srcpads", channels)
    rtpsink1 = Gst.Element.get_request_pad(rtpsession1, "send_rtp_sink")
    rtpsink2 = Gst.Element.get_request_pad(rtpsession2, "send_rtp_sink")
    rtpsink3 = Gst.Element.get_request_pad(rtpsession3, "send_rtp_sink")
    rtpsrc1 = Gst.Element.get_static_pad(rtpsession1, "send_rtp_src")
    rtpsrc2 = Gst.Element.get_static_pad(rtpsession2, "send_rtp_src")
    rtpsrc3 = Gst.Element.get_static_pad(rtpsession3, "send_rtp_src")
    # udp
    udpsink1 = Gst.ElementFactory.make('udpsink', None)
    udpsink1.set_property("host", REM_IP)
    udpsink1.set_property("port", 8000)
    usink1 = Gst.Element.get_static_pad(udpsink1, "sink")
    udpsink2 = Gst.ElementFactory.make('udpsink', None)
    udpsink2.set_property("host", REM_IP)
    udpsink2.set_property("port", 9000)
    usink2 = Gst.Element.get_static_pad(udpsink2, 'sink')
    udpsink3 = Gst.ElementFactory.make('udpsink', None)
    udpsink3.set_property("host", REM_IP)
    udpsink3.set_property("port", 10000)
    usink3 = Gst.Element.get_static_pad(udpsink3, "sink")
    #rtcp
    rtcp_caps = Gst.Caps.from_string("application/x-rtcp")
    udpsrc1 = Gst.ElementFactory.make('udpsrc', None)
    udpsrc1.set_property("port", 8003)
    udpsrc1.set_property("caps", rtcp_caps)
    usrc1 = Gst.Element.get_static_pad(udpsrc1, "src")
    udpsrc2 = Gst.ElementFactory.make('udpsrc', None)
    udpsrc2.set_property("port", 9003)
    udpsrc2.set_property("caps", rtcp_caps)
    usrc2 = Gst.Element.get_static_pad(udpsrc2, "src")
    udpsrc3 = Gst.ElementFactory.make('udpsrc', None)
    udpsrc3.set_property("port", 10003)
    udpsrc3.set_property("caps", rtcp_caps)
    usrc3 = Gst.Element.get_static_pad(udpsrc3, "src")
    rtcpsink1 = Gst.Element.get_request_pad(rtpsession1, "recv_rtcp_sink")
    rtcpsink2 = Gst.Element.get_request_pad(rtpsession2, "recv_rtcp_sink")
    rtcpsink3 = Gst.Element.get_request_pad(rtpsession3, "recv_rtcp_sink")
    rtcpsrc1 = Gst.Element.get_request_pad(rtpsession1, "send_rtcp_src")
    rtcpsrc2 = Gst.Element.get_request_pad(rtpsession2, "send_rtcp_src")
    rtcpsrc3 = Gst.Element.get_request_pad(rtpsession3, "send_rtcp_src")
    udpsink4 = Gst.ElementFactory.make('udpsink', None)
    udpsink4.set_property("host", REM_IP)
    udpsink4.set_property("port", 8001)
    udpsink4.set_property('async', False)
    udpsink4.set_property('sync', False)
    usink4 = Gst.Element.get_static_pad(udpsink4, 'sink')
    udpsink5 = Gst.ElementFactory.make('udpsink', None)
    udpsink5.set_property("host", REM_IP)
    udpsink5.set_property("port", 9001)
    udpsink5.set_property('async', False)
    udpsink5.set_property('sync', False)
    usink5 = Gst.Element.get_static_pad(udpsink5, "sink")
    udpsink6 = Gst.ElementFactory.make('udpsink', None)
    udpsink6.set_property("host", REM_IP)
    udpsink6.set_property("port", 10001)
    udpsink6.set_property('async', False)
    udpsink6.set_property('sync', False)
    usink6 = Gst.Element.get_static_pad(udpsink6, "sink")
    
    # Add elements to pipeline
    pipeline.add(src)
    pipeline.add(conv)
    pipeline.add(camerafilter)
    pipeline.add(encoder)
    pipeline.add(parse)
    pipeline.add(pay)
    pipeline.add(rr)
    pipeline.add(queue1)
    pipeline.add(queue2)
    pipeline.add(queue3)
    pipeline.add(rtpsession1)
    pipeline.add(rtpsession2)
    pipeline.add(rtpsession3)
    pipeline.add(udpsink1)
    pipeline.add(udpsink2)
    pipeline.add(udpsink3)
    pipeline.add(udpsink4)
    pipeline.add(udpsink5)
    pipeline.add(udpsink6)
    pipeline.add(udpsrc1)
    pipeline.add(udpsrc2)
    pipeline.add(udpsrc3)
    
    # Link all elements
    src.link(conv)
    conv.link(camerafilter)
    camerafilter.link(encoder)
    encoder.link(parse)
    parse.link(pay)
    pay.link(rr)
    #   canale 1
    Gst.Pad.link(rrpad1, qsink1)
    Gst.Pad.link(qsrc1, rtpsink1)
    Gst.Pad.link(rtpsrc1, usink1)
    #   canale 2
    Gst.Pad.link(rrpad2, qsink2)
    Gst.Pad.link(qsrc2, rtpsink2)
    Gst.Pad.link(rtpsrc2, usink2)
    #   canale 3
    Gst.Pad.link(rrpad3, qsink3)
    Gst.Pad.link(qsrc3, rtpsink3)
    Gst.Pad.link(rtpsrc3, usink3)
    #   rtcp_1
    Gst.Pad.link(usrc1, rtcpsink1)
    Gst.Pad.link(rtcpsrc1, usink4)
    #   rtcp_2
    Gst.Pad.link(usrc2, rtcpsink2)
    Gst.Pad.link(rtcpsrc2, usink5)
    #   rtcp_3
    Gst.Pad.link(usrc3, rtcpsink3)
    Gst.Pad.link(rtcpsrc3, usink6)
    
    # Change probabilities
    GLib.timeout_add_seconds(3, controller)

    # Run the pipeline
    pipeline.set_state(Gst.State.PLAYING)
    try:
        loop.run()
    except Exception as e:
        print(e)

    # Clean-up
    pipeline.set_state(Gst.State.NULL)
