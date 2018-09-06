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




/**
Stores the location and length of a memory-mapped frame.
*/
typedef struct CameraBuffer {
    void *start;    ///< Pointer to memory-mapped frame.
    size_t length;  ///< Length in bytes of memory-mapped frame.
} camera_buffer;
camera_buffer *buffered_camera;

struct pollfd fds;  ///< File descriptor for mapped-memory with new frames.
/*
Variables associated with frame capture.
*/
struct v4l2_format fmt = {0}; ///< v4l2 format video stream struct.
struct v4l2_requestbuffers req = {0}; ///< v4l2 request access to buffer struct.
struct v4l2_buffer buf = {0};   ///< v4l2 buffer struct.

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

int initialize() {
    int rc;

    // Open desired streaming device.
    fds.fd = open("/dev/video1", O_RDWR);
    if (fds.fd == -1) {
        perror("Opening video device");
        return -1;
    }

    // Setup video capture format for MJPEG with 640x480 resolution.
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    rc = xioctl(fds.fd, VIDIOC_S_FMT, &fmt);
    if (rc == -1) {
        perror("Setting pixel format");
        return -1;
    }

    // Request 20 memory-mapped buffers.
    req.count = 20;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    rc = xioctl(fds.fd, VIDIOC_REQBUFS, &req);
    if (rc == -1) {
        perror("Requesting buffer");
        return -1;
    }

    // Allocate memory for buffered_camera for pointers to memory-mapped frames.
    buffered_camera = (camera_buffer *)calloc(req.count, sizeof(camera_buffer));
    if (buffered_camera == NULL) {
        perror("calloc - buffered_camera - error");
        return -1;
    }

    // Get pointers to all buffers and map buffered_camera to each buffer.
    // This gives us a pointer to where the frames are being stored.
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (uint i = 0; i < req.count; i++) {
        buf.index = i;
        rc = xioctl(fds.fd, VIDIOC_QUERYBUF, &buf);
        if (rc == -1) {
            perror("Querying buffer");
            return -1;
        }
        buffered_camera[i].length = buf.length;
        buffered_camera[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fds.fd, buf.m.offset);
    }

    // Query all memory-mapped buffers into the streaming queue.
    // Frames aren't saved unless there are queued buffers.
    for (uint i = 0; i < req.count; i++) {
        buf.index = i;
        rc = xioctl(fds.fd, VIDIOC_QBUF, &buf);
        if (rc == -1) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    // Command device to begin streaming.
    // This puts frames into the queued buffers.
    rc = xioctl(fds.fd, VIDIOC_STREAMON, &buf.type);
    if (rc == -1) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    // Process a single frame and store actual frame resolution.
    // poll will block until either a frame is stored in a buffer or
    // 60000 milliseconds have elapsed.
    fds.events = POLLIN;
    rc = poll(&fds, 1, 60000);
    if (rc == -1) {
        perror("poll error");
        return -1;
    } else if (rc == 0) {
        perror("poll timeout - init");
        return -1;
    }

    // Remove buffered memory from streaming video queue.
    // If we don't remove the buffer from the queue, it could get overwritten.
    rc = xioctl(fds.fd, VIDIOC_DQBUF, &buf);
    if (rc == -1) {
        perror("VIDIOC_DQBUF");
        return -1;
    }

    // #######################################################################
    // This is where we can do what we want with the frame. Let's save it
    // to a file.
    FILE *file;
    file = fopen("./frames/frame_init.jpeg", "wb");
    fwrite(buffered_camera[buf.index].start, sizeof(unsigned char), buf.bytesused, file);
    fclose(file);
    // #######################################################################

    // Queue buffered memory.
    rc = xioctl(fds.fd, VIDIOC_QBUF, &buf);
    if (rc == -1) {
        perror("VIDIOC_QBUF");
        return -1;
    }

    return 0;
}

// Just copying the last 3 steps from above, we can capture frames and do what
// we want with them in a loop.
int loop() {
    int rc;
    FILE *file;
    char filename[128];

    for (int i = 0; i < 10; i++) {
        // Process a single frame and store actual frame resolution.
        // poll will block until either a frame is stored in a buffer or
        // 60000 milliseconds have elapsed.
        fds.events = POLLIN;
        rc = poll(&fds, 1, 1000);
        if (rc == -1) {
            perror("poll error");
            return -1;
        } else if (rc == 0) {
            perror("poll timeout - init");
            return -1;
        }

        // Remove buffered memory from streaming video queue.
        // If we don't remove the buffer from the queue, it could get overwritten.
        rc = xioctl(fds.fd, VIDIOC_DQBUF, &buf);
        if (rc == -1) {
            perror("VIDIOC_DQBUF");
            return -1;
        }

        // ###################################
        // This is where we can do what we want with the frame. Let's save it
        // to a file.
        sprintf(filename, "./frames/frame_%04d.jpeg", i);
        file = fopen(filename, "wb");
        fwrite(buffered_camera[buf.index].start, sizeof(unsigned char), buf.bytesused, file);
        fclose(file);
        // ###################################

        // Queue buffered memory.
        rc = xioctl(fds.fd, VIDIOC_QBUF, &buf);
        if (rc == -1) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (initialize() == -1) exit(EXIT_FAILURE);
    loop();
    exit(EXIT_SUCCESS);
};
