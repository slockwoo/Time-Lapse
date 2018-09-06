# Time-lapse

This is a time-lapse software created for a project in ECEN 5623 - Real-Time Embedded Systems. Some high level requirements include capturing images at 1 Hz, converting them to .ppm format, appending timestamp and system information to the images' header, and saving the images. At least 2 threads needed to be used, and rate-monotonic policy had to be followed.

### Subsets

Subsets of the program are displayed in separate folders.

* [video_capture](https://github.com/slockwoo/Time-Lapse/tree/master/video_capture) - Uses Linux's v4l2 to capture JPEG images from a MJPEG stream
and saves them to files.
* [decode_jpeg](https://github.com/slockwoo/Time-Lapse/tree/master/decode_jpeg) - Decodes a JPEG image into an RGB image.
* [zlib_compression](https://github.com/slockwoo/Time-Lapse/tree/master/zlib_compression) - Compresses a RGB image using zlib.
