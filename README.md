# ixgnpreader

A GStreamer element to measure Bitrate, CPU usage

## Build Instructions

On Debian-based systems:
```bash
./autogen.sh
./configure --prefix /usr/ --libdir /usr/lib/x86_64-linux-gnu/
make
sudo make install
```


## Usage

gst-launch-1.0 -e videotestsrc ! x264enc ! perf ! qtmux print-arm-load=true ! filesink location=test.mp4

