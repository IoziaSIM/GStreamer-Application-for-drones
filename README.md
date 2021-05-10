#Installation of the system in UBUNTU

Create 2 directory in the directory where there are the pipelines, one named "path", other name "gstreamer".

In the "gstreamer" directory :

1. Download the build of GStreamer from https://github.com/GStreamer/gst-build 
2. Download the program Meson (https://mesonbuild.com/) with "sudo pip3 install meson"
3. Download the program Ninja with "sudo pip3 install ninja"
4. Go to the directory gst-build and compile gst-build with "meson build" 
5. Install the possible missing dependencies
6. Compile build with "ninja -C build"  
7. Again install the possible missing dependencies
8. Replace "gstroundrobin.c", "gstrtpsession.c" (for the sender) and "gstrtpptdemux.c", "gstrtpstorage.c" (for the receiver) in the "subprojects" directory with corresponding file in "modified plugins"
9. Again compile the system with "ninja -C build"
10. In order to create an environment in order to execute pipeline, run "ninja -C build uninstalled"
11. Return to the directory with the pipelines
12. Run "python3 sender.py" and "./receiver.sh" to execute the system

In the "path" directory, during the execution, the sender will create "ploss.txt" with the number of lost packet of each channel: the first number is the packet loss of first channel, the second number is the packet loss of second channel, etc.

If you have to modify some plug-ins, after always run "ninja -C build".

If you have to modify IP address, modify the first lines of "sender.py", the IP addresses of "receiver.sh" and the IP address for the bind function in "rtpstorage.c" (remember to run "ninja -C build after).

In order to search the changes in the modified plug-ins, search the word "changes" in the file (except for "gstroundrobin.c", that is almost all new).
