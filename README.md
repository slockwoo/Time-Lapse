# Time-lapse

This is a time-lapse software created for a project in ECEN 5623 - Real-Time Embedded Systems. Some high level requirements include capturing images at 1 Hz, converting them to .ppm format, appending timestamp and system information to the images' header, and saving the images. At least 2 threads needed to be used, and rate-monotonic policy had to be followed.

Example launch command

```
sudo ./capture -f 1 -F 10 -t 3 -w -V
```

sudo is required on Linux to create real-time threads which this program uses.
This command will capture 10 frames at 1 Hz. It will use all 3 threads, so it
will capture a JPEG image, decode it to PPM, then compress it to zlib. It will
transfer the images via FTP to the target machine, and it will print the frames
captured every second.

### Subsets

Subsets of the program are displayed in separate folders.

* [video_capture](https://github.com/slockwoo/Time-Lapse/tree/master/video_capture) - Uses Linux's v4l2 to capture JPEG images from a MJPEG stream
and saves them to files.
* [decode_jpeg](https://github.com/slockwoo/Time-Lapse/tree/master/decode_jpeg) - Decodes a JPEG image into an RGB image.
* [zlib_compression](https://github.com/slockwoo/Time-Lapse/tree/master/zlib_compression) - Compresses an RGB image using zlib.
* [curl_tranfer](https://github.com/slockwoo/Time-Lapse/tree/master/curl_transfer) - Transfer a file to a target machine via FTP on libcurl.
