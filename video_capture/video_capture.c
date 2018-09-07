// This program demonstrates capturing frames from a MJPEG video stream (camera)
// using v4l2.

#include <stdlib.h>
#include <stdio.h>
#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

// If your camera doesn't support this resolution, the images will be saved
// with a different resolution. Be mindful of that if you intend to continue
// processing the same images.
// Despite initializing with 640x480 resolution, my laptop's camera outputs
// 960x540 images.
#define WIDTH 640
#define HEIGHT 480

/*
Stores the location and length of a memory-mapped frame.
*/
struct CameraBuffer {
    void *start;    // Pointer to memory-mapped frame.
    size_t length;  // Length in bytes of memory-mapped frame.
};

/*
Convenience function to repeatedly send ioctl commands to device until it is
available and responds.
*/
static int xioctl(int fd, int request, void *arg) {
    int rc;
    do rc = ioctl(fd, request, arg);
    while (-1 == rc && EINTR == errno);
    return rc;
}

int main(int argc, char** argv) {
    int rc;
    FILE *file;
    char filename[128];
    struct CameraBuffer buffered_camera[20];
    struct pollfd fds;  // File descriptor for mapped-memory with new frames.
    struct v4l2_format fmt = {0}; // v4l2 format video stream struct.
    struct v4l2_requestbuffers req = {0}; // v4l2 request access to buffer struct.
    struct v4l2_buffer buf = {0};   // v4l2 buffer struct.


    // Open desired streaming device.
    fds.fd = open("/dev/video0", O_RDWR);

    // Setup video capture format for MJPEG with 640x480 resolution.
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    xioctl(fds.fd, VIDIOC_S_FMT, &fmt);

    // Request 20 memory-mapped buffers.
    req.count = 20;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    xioctl(fds.fd, VIDIOC_REQBUFS, &req);

    // Get pointers to all buffers and map buffered_camera to each buffer.
    // This gives us a pointer to where the frames are being stored.
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (uint i = 0; i < req.count; i++) {
        buf.index = i;
        xioctl(fds.fd, VIDIOC_QUERYBUF, &buf);
        buffered_camera[i].length = buf.length;
        buffered_camera[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fds.fd, buf.m.offset);
    }

    // Query all memory-mapped buffers into the streaming queue.
    // Frames aren't saved unless there are queued buffers.
    for (uint i = 0; i < req.count; i++) {
        buf.index = i;
        xioctl(fds.fd, VIDIOC_QBUF, &buf);
    }

    // Command device to begin streaming.
    // This puts frames into the queued buffers.
    xioctl(fds.fd, VIDIOC_STREAMON, &buf.type);

    // Process a single frame and store actual frame resolution.
    // poll will block until either a frame is stored in a buffer or
    // 60000 milliseconds have elapsed.
    fds.events = POLLIN;
    poll(&fds, 1, 60000);

    // Remove buffered memory from streaming video queue.
    // If we don't remove the buffer from the queue, it could get overwritten.
    xioctl(fds.fd, VIDIOC_DQBUF, &buf);

    // #######################################################################
    // This is where we can do what we want with the frame. Let's save it
    // to a file.
    file = fopen("../frames/jpeg/frame_init.jpeg", "wb");
    fwrite(buffered_camera[buf.index].start, sizeof(unsigned char), buf.bytesused, file);
    fclose(file);
    // #######################################################################

    // Queue buffered memory.
    xioctl(fds.fd, VIDIOC_QBUF, &buf);


    // Let's loop to capture 10 more frames.
    for (int i = 0; i < 10; i++) {
        fds.events = POLLIN;
        poll(&fds, 1, 1000);

        xioctl(fds.fd, VIDIOC_DQBUF, &buf);

        sprintf(filename, "../frames/jpeg/frame_%04d.jpeg", i);
        file = fopen(filename, "wb");
        fwrite(buffered_camera[buf.index].start, sizeof(unsigned char), buf.bytesused, file);
        fclose(file);

        xioctl(fds.fd, VIDIOC_QBUF, &buf);
    }


    // Command device to stop streaming.
    rc = xioctl(fds.fd, VIDIOC_STREAMOFF, &buf.type);
    if (rc == -1) {
        perror("VIDIOC_STREAMOFF error");
    }

    // Unmap all memory-mapped buffers
    for (uint i = 0; i < req.count; i++) {
        buf.index = i;
        rc = munmap(buffered_camera[i].start, buf.length);
        if (rc == -1) {
            perror("munmap error");
        }
    }

    return 0;
}
