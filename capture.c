
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <linux/videodev2.h>
#include <semaphore.h>
#include <mqueue.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include <sys/utsname.h>
#include <time.h>
#include <poll.h>
#include <curl/curl.h>
#include <jpeglib.h>
#include <turbojpeg.h>

#define STRING_SIZE 127
#define CAMERA_MQ "/camera_mq"
#define COMPRESSION_MQ "/compression_mq"
#define NSEC_IN_SEC 1000000000
#define MSG_SIZE 32
#define HEADER_SIZE 1024

#define TARGET_ADDRESS "192.168.137.2/Documents/ECEN5623/stream_mjpeg/frames/"
#define CREDENTIALS "shane:Go0b3r@#"
// #define TARGET_ADDRESS "192.168.137.3/frames/"
// #define CREDENTIALS "debian:temppwd"
// #define TARGET_ADDRESS "192.168.137.4/Documents/EX06/v4l2_project/frames/"
// #define CREDENTIALS "pi:raspberry"

/**
Stores the location and length of a memory-mapped frame.
*/
typedef struct CameraBuffer {
    void *start;    ///< Pointer to memory-mapped frame.
    size_t length;  ///< Length in bytes of memory-mapped frame.
} camera_buffer;
camera_buffer *buffered_camera;

/**
Stores the location and length of an image.
*/
struct Frame {
    unsigned char *data;    ///< Pointer to stored image.
    size_t size;    ///< Length in bytes of stored image.
};

/**
Stores all versions of an image with the time it was received.
*/
typedef struct FrameBuffer {
    struct Frame *frame;    ///< Pointer to array of Frames
    struct timespec timestamp;  ///< Timestamp of frame in device time.
} frame_buffer;
frame_buffer *buffered_frame;

/**
Stores a synchronized time between the computer and camera to accurately
calculate the time of each frame relative to the computer's time when embedding
the frame's timestamp into their respective headers.
*/
struct Time {
    struct timespec computer;   ///< Computer time when first frame was captured.
    struct timespec camera;     ///< Streaming device time when first frame was captured.
} initial_time;

int frame_buffer_size;  ///< Size of buffered_frame array.
int compression_mq_size = 0;    ///< Number of messages in compression message queue.
/*
Variables associated with saving images to file.
*/
char ppm_header[HEADER_SIZE];   //< Array for storing PPM header.
char filename[128];     ///< Array for storing file names.
char extension[10];     ///< Array for storing file extension.
struct utsname unm;     //< Struct for storing system information
char time_buffer[32];   ///< Buffer for storing frame timestamp as a char array
size_t temp_bytesused;  ///< Temporary variable for returning image pointer when saving images to file.
struct Frame *upload;   ///< Struct for referencing data when transmitting data via network.
size_t max, copylen;    ///< Variables used for tracking data size when transmitting data via network.
/*
Variables associated with command-line options
*/
int cam = 0;        ///< Desired video stream device.
int hres = 640;     ///< Horizonal resolution.
int vres = 480;     ///< Vertical resolution.
int fps = 30;       ///< Target video capture rate
int max_frames = 2000;    ///< Maximum number of frames before exiting program.
int num_threads = INT_MAX;  ///< Number of threads to run.
int verbose = 0;    ///< Boolean for printing frames captured per second to standard output.
int write_file = 0; ///< Boolean for  saving images to file.
/*
Variables associated with jpeg decompression
*/
struct jpeg_decompress_struct cinfo;    ///< libjpeg decompression struct
struct jpeg_error_mgr jerr;             ///< libjpeg error struct
struct jpeg_source_mgr src;             ///< libjpeg source struct
/*
Variables associated with file transfer via network
*/
CURL *curl;     ///< libcurl variable.
CURLcode res;   ///< libcurl variable.

struct pollfd fds;  ///< File descriptor for mapped-memory with new frames.
/*
Variables associated with frame capture.
*/
struct v4l2_format fmt = {0}; ///< v4l2 format video stream struct.
struct v4l2_requestbuffers req = {0}; ///< v4l2 request access to buffer struct.
struct v4l2_buffer buf = {0};   ///< v4l2 buffer struct.

sem_t camera_sem;       ///< Semaphore to control acquire_frames thread with sequencer.
sem_t embed_ppm_sem;    ///< Semaphore to control embed_ppm thread with sequencer.
sem_t compression_sem;  ///< Semaphore to control compression thread with sequencer.
mqd_t camera_mq;        ///< Message queue from acquire_frames thread to embed_ppm thread.
mqd_t compression_mq;   ///< Message queue from embed_ppm thread to compression thread.
struct mq_attr mq_attr = {0}; ///< Message queue attributes struct.

/**
Calculates difference is two timespec structures and stores the difference
into a third timspec structure.
@param start Pointer to the earlier timespec.
@param stop Pointer to the later timespec.
@param diff Pointer to the timespec the difference will be saved to. Can be the same as one of the other passed pointers.
*/
void *time_diff(struct timespec *start, struct timespec *stop, struct timespec *diff) {
    diff->tv_sec = stop->tv_sec - start->tv_sec;
    diff->tv_nsec = stop->tv_nsec - start->tv_nsec;
    if (diff->tv_nsec < 0) {
        diff->tv_nsec += NSEC_IN_SEC;
        diff->tv_sec -= 1;
    }
    return (void *)0;
}

/**
Converts a timespec structure to nanoseconds as a long long.
Long long is necessary for architecures with 4 byte longs.
A variable with a deterministic number of bytes would be preferable.
@param t Pointer to a timespec.
@return  Returns the nanoseconds as a long long.
*/
long long timespec_to_long_long(struct timespec *t) {
    return ((long long)t->tv_sec * (long long)NSEC_IN_SEC) + (long long)t->tv_nsec;
}

/**
Converts a timeval structure to nanoseconds as a long long.
@param t Pointer to a timeval.
@return  Returns the nanoseconds as a long long.
*/
long long timeval_to_long_long(struct timeval *t) {
    return ((long long)t->tv_sec * (long long)NSEC_IN_SEC) + (long long)t->tv_usec*1000;
}

/**
Converts nanoseconds as a long long to a timespec structure.
@param t Pointer to a timespec.
@param nanoseconds Time in nanoseconds.
*/
void *long_long_to_timespec(struct timespec *t, long long nanoseconds) {
    if (nanoseconds >= NSEC_IN_SEC) {
        t->tv_sec = nanoseconds / NSEC_IN_SEC;
        nanoseconds -= t->tv_sec * NSEC_IN_SEC;
    }
    t->tv_nsec = nanoseconds;
    return (void *)0;
}

/**
Replaces libcurl's read_callback function.
Iteratively copies an image to *ptr to allow libcurl to transmit the image
via network to ultimately be saved on a remote machine.
@param ptr Pointer to destination for data to be transferred.
@param size Size in bytes of each element to be transferred.
@param nmemb Number of elements to be transferred.
@param userp Pointer to source to data to be transferred.
@return Number of bytes successfully transferred.
*/
static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp) {
    upload = (struct Frame *)userp;
    max = size*nmemb;
    if (upload->size < 1) return 0;

    if (max > upload->size) copylen = upload->size;
    else copylen = max;

    memcpy(ptr, upload->data, copylen);
    upload->data += copylen;
    upload->size -= copylen;
    return copylen;

    return 0;
}

/**
Setup libcurl to transfer an image to a remote machine.
@param index Index in buffered_frame containing image to be transferred.
@param frame Frame number to uniquely identify the image.
*/
void *save_image(unsigned int index, unsigned int frame) {

    temp_bytesused = buffered_frame[index].frame[num_threads-1].size;
    sprintf(filename, "ftp://%sframe_%04d%s", TARGET_ADDRESS, frame, extension);
    res = curl_easy_setopt(curl, CURLOPT_URL, filename);
    if (res != CURLE_OK) fprintf(stderr, "CURLOPT_URL error: %s\n", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_READDATA, &buffered_frame[index].frame[num_threads-1]);
    if (res != CURLE_OK) fprintf(stderr, "CURLOPT_READDATA error: %s\n", curl_easy_strerror(res));
    res = curl_easy_setopt(curl, CURLOPT_INFILESIZE, (curl_off_t)buffered_frame[index].frame[num_threads-1].size);
    if (res != CURLE_OK) fprintf(stderr, "CURLOPT_INFILESIZE error: %s\n", curl_easy_strerror(res));
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) fprintf(stderr, "curl_easy_perform error: %s\n", curl_easy_strerror(res));

    buffered_frame[index].frame[num_threads-1].data -= temp_bytesused;
    buffered_frame[index].frame[num_threads-1].size = temp_bytesused;

    return 0;
}

/**
Convert image from RGB to PPM. Appends image's timestamp and the system's
information to the header of the PPM image.
@param index Index to buffered_frame contianing image to be converted.
*/
void *embed_ppm_header(unsigned int index) {
    time_diff(&initial_time.camera, &buffered_frame[index].timestamp, &buffered_frame[index].timestamp);
    long_long_to_timespec(&buffered_frame[index].timestamp, timespec_to_long_long(&initial_time.computer) + timespec_to_long_long(&buffered_frame[index].timestamp));

    strftime(time_buffer, sizeof(time_buffer), "%T", localtime(&buffered_frame[index].timestamp.tv_sec));

    sprintf(ppm_header, "P6\n# %s.%09ld\n# %s %s %s %s %s\n%d %d\n255\n", time_buffer, buffered_frame[index].timestamp.tv_nsec, unm.sysname, unm.nodename, unm.release, unm.version, unm.machine, cinfo.output_width, cinfo.output_height);

    memmove((void *)buffered_frame[index].frame[1].data + sizeof(char)*strlen(ppm_header), (void *)buffered_frame[index].frame[1].data, buffered_frame[index].frame[1].size);
    memmove((void *)buffered_frame[index].frame[1].data, (void *)ppm_header, sizeof(char)*strlen(ppm_header));
    buffered_frame[index].frame[1].size += HEADER_SIZE;

    return 0;
}

/*
Skeleton functions necessary to access JPEG header information in Initialize().
*/
void init_source(j_decompress_ptr cinfo) {}
boolean fill_input_buffer(j_decompress_ptr cinfo) {return TRUE;}
void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {}
void term_source(j_decompress_ptr cinfo) {}
void emit_message(j_common_ptr cinfo, int msg_level) {}

/**
Convenience function to repeatedly send ioctl commands to device until it is
available and responds.
@param fd Device file descriptor.
@param request Command to be sent to device.
@param arg Pointer to variable to store information returned from device.
@return ioctl() return code.
*/
static int xioctl(int fd, int request, void *arg) {
    int r;
    do r = ioctl(fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

/**
Parses command line arguments and sets variables accordingly.
Will print a usage statement and exit program if passed improperly formatted arguments.
- -c [integer]      Device to stream video from.
- -f [integer]      Desired frame rate. Rates less than 1 Hz have not been tested.
- -F [integer]      Maximum desired frames to capture.
- -h [integer]      Horizontal resolution.
- -t [integer]      Number of threads to run.
    - 1                 Capture JPEG from stream.
    - 2                 Convert JPEG to PPM.
    - 3                 Compress PPM to zlib format.
    .
- -v [integer]      Vertical resolution.
- -V                Print frames captured per second to standard output.
- -w                Save images. Will transfer over network and save on remote machine.
.
@param argc Number of tokens passed from the command line.
@param argv Pointer to array of tokens passed from the command line.
@return Return code for successful completion of all tasks.
*/
int accept_options(int argc, char** argv) {
    int rc = 1; // variable for storing return codes
    int error = 0;  // Records if any errors in the command line input exist
    // Cycle through all options passed via the command line
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'c':   // Camera selection
                    if (argc <= i+1) error = 1;
                    else rc = sscanf(argv[i+1], "%d", &cam);
                    if (rc != 1) error = 1;
                    break;
                case 'f':   // Desired frame rate
                    if (argc < i+1) error = 1;
                    else rc = sscanf(argv[i+1], "%d", &fps);
                    if (rc != 1) error = 1;
                    break;
                case 'F':   // Maximum frames before exiting
                    if (argc < i+1) error = 1;
                    else rc = sscanf(argv[i+1], "%d", &max_frames);
                    if (rc != 1) error = 1;
                    break;
                case 'h':   // Horizontal resolution
                    if (argc <= i+1) error = 1;
                    else rc = sscanf(argv[i+1], "%d", &hres);
                    if (rc != 1) error = 1;
                    break;
                case 't':
                    if (argc <= i+1) error = 1;
                    else rc = sscanf(argv[i+1], "%d", &num_threads);
                    if (rc <= 0) error = 1;
                    break;
                case 'v':   // Vertical resolution
                    if (argc <= i+1) error = 1;
                    else rc = sscanf(argv[i+1], "%d", &vres);
                    if (rc != 1) error = 1;
                    break;
                case 'V':   // Print extra outputs to standard output
                    verbose = 1;
                    break;
                case 'w':   // Save image to file
                    write_file = 1;
                    break;
            }
        }
        // If the command line input is not understood, print usage to standard
        // output and exit program.
        if (error) {
            printf("Usage -c [camera] -f [frame rate] -F [maximum frames] -h [horizontal resolution] -t [number of threads] -v [vertical resolution] -V -w\n");
            return -1;
        }
    }
    return 0;
}

/**
Initialize all global objects and variables.
- Gather system information for embedding in PPM images.
- Open streaming device.
- Setup video capture format
- Setup memory-mapped buffer requirements.
- Initialize frame_buffer_size. Size is determined by number of threads and desired frame rate.
- Allocate memory for buffered_camera for pointers to memory-mapped frames.
- Allocate memory for buffered_frame to store images.
- Setup libcurl to transfer images over network.
- Initialize file extension depending on number of threads.
- Setup memory-mapped buffer and map buffered_camera to memory.
- Queue all mapped memory to accept new frames.
- Command device to begin streaming.
- Process a single frame and store actual frame resolution.
- Print camera properties to standard output.
- Initialize semaphores for sequencer control over threads.
- Initalize message queues for passing frame pointers between threads.
.
@return Return code for successful completion of all tasks.
*/
int initialize() {
    int rc = 1; // store return codes
    char temp_string[STRING_SIZE];   // handle all strings

    // Store system information in unm.
    rc = uname(&unm);
    if (rc == -1) {
        perror("uname error");
        return -1;
    }

    // Open desired streaming device.
    sprintf(temp_string, "/dev/video%d", cam);
    fds.fd = open(temp_string, O_RDWR);
    if (fds.fd == -1) {
        perror("Opening video device");
        return -1;
    }

    // Setup video capture format for MJPEG.
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = hres;
    fmt.fmt.pix.height = vres;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    rc = xioctl(fds.fd, VIDIOC_S_FMT, &fmt);
    if (rc == -1) {
        perror("Setting pixel format");
        return -1;
    }

    // Setup memory-mapped buffer requirements.
    req.count = fps*2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    rc = xioctl(fds.fd, VIDIOC_REQBUFS, &req);
    if (rc == -1) {
        perror("Requesting buffer");
        return -1;
    }

    // Initialize frame_buffer_size. Size is determined by number of threads and desired frame rate.
    frame_buffer_size = num_threads*fps*4;

    // Allocate memory for buffered_camera for pointers to memory-mapped frames.
    buffered_camera = (camera_buffer *)calloc(req.count, sizeof(camera_buffer));
    if (buffered_camera == NULL) {
        perror("calloc - buffered_camera - error");
        return -1;
    }

    // Allocate memory for buffered_frame to store images.
    buffered_frame = (frame_buffer *)calloc(frame_buffer_size, sizeof(frame_buffer));
    if (buffered_frame == NULL) {
        perror("calloc - buffered_frame - error");
        return -1;
    }
    for (int i = 0; i < frame_buffer_size; i++) {
        buffered_frame[i].frame = (struct Frame *)calloc(num_threads, sizeof(struct Frame));
        if (buffered_frame[i].frame == NULL) {
            perror("malloc - buffered_frame.frame - error");
            return -1;
        }
        for (int j = 0; j < num_threads; j++) {
            buffered_frame[i].frame[j].data = (unsigned char *)malloc(1280*960*3*sizeof(unsigned char));
            if (buffered_frame[i].frame[j].data == NULL) {
                perror("malloc - buffered_frame.frame.data - error");
                return -1;
            }
            buffered_frame[i].frame[j].size = 1;
        }
    }

    // Setup libcurl to transfer images over network.
    res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_global_init error: %s\n", curl_easy_strerror(res));
        return -1;
    }
    curl = curl_easy_init();
    if (curl) {
        res = curl_easy_setopt(curl, CURLOPT_USERPWD, CREDENTIALS);
        if (res != CURLE_OK) fprintf(stderr, "CURLOPT_USERPWD error: %s\n", curl_easy_strerror(res));
        res = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        if (res != CURLE_OK) fprintf(stderr, "CURLOPT_UPLOAD error: %s\n", curl_easy_strerror(res));
        res = curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
        if (res != CURLE_OK) fprintf(stderr, "CURLOPT_READFUNCTION error: %s\n", curl_easy_strerror(res));
    } else {
        fprintf(stderr, "curl_easy_init error\n");
        return -1;
    }

    // Initialize file extension depending on number of threads.
    if (num_threads <= 1) sprintf(extension, "%s", ".jpeg");
    if (num_threads == 2) sprintf(extension, "%s", ".ppm");
    if (num_threads >= 3) sprintf(extension, "%s", ".ppm.zlib");

    // Setup memory-mapped buffer and map buffered_camera to memory.
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

    // Queue all mapped memory to accept new frames.
    for (uint i = 0; i < req.count; i++) {
        buf.index = i;
        rc = xioctl(fds.fd, VIDIOC_QBUF, &buf);
        if (rc == -1) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    // Command device to begin streaming.
    rc = xioctl(fds.fd, VIDIOC_STREAMON, &buf.type);
    if (rc == -1) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    // Process a single frame and store actual frame resolution.
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
    rc = xioctl(fds.fd, VIDIOC_DQBUF, &buf);
    if (rc == -1) {
        perror("VIDIOC_DQBUF");
        return -1;
    }
    // Setup jpeg_source_mgr.
    src.init_source = init_source;
    src.fill_input_buffer = fill_input_buffer;
    src.skip_input_data = skip_input_data;
    src.term_source = term_source;
    // Acquire JPEG header information for resolution.
    cinfo.err = jpeg_std_error(&jerr);
    cinfo.err->emit_message = emit_message;
    jpeg_create_decompress(&cinfo);
    src.next_input_byte = (const JOCTET *)buffered_camera[buf.index].start;
    src.bytes_in_buffer = buf.bytesused;
    cinfo.src = &src;
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);
    // Store frame resolution.
    hres = cinfo.output_width;
    vres = cinfo.output_height;
    jpeg_destroy_decompress(&cinfo);
    // Queue buffered memory.
    rc = xioctl(fds.fd, VIDIOC_QBUF, &buf);
    if (rc == -1) {
        perror("VIDIOC_QBUF");
        return -1;
    }

    // Print camera properties to standard output.
    printf("Camera: %d\n", cam);
    printf("Horizontal: %d\n", hres);
    printf("Vertical: %d\n", vres);
    printf("Frame rate: %d\n", fps);
    printf("Frame limit: %d\n", max_frames);
    printf("\n");

    // Initialize semaphores for sequencer control over threads.
    rc = sem_init(&camera_sem, 0, 0);
    if (camera_mq == -1) {
        perror("sem_init - camera_sem - error.\n");
        return -1;
    }
    rc = sem_init(&embed_ppm_sem, 0, 0);
    if (camera_mq == -1) {
        perror("sem_init - embed_ppm_sem - error");
        return -1;
    }
    rc = sem_init(&compression_sem, 0, 0);
    if (camera_mq == -1) {
        perror("sem_init - compression_sem - error");
        return -1;
    }

    // Initalize message queues for passing frame pointers between threads.
    mq_attr.mq_flags = 0;
    mq_attr.mq_maxmsg = frame_buffer_size-1;
    mq_attr.mq_msgsize = MSG_SIZE;
    mq_attr.mq_curmsgs = 0;
    camera_mq = mq_open(CAMERA_MQ, O_CREAT|O_RDWR|O_NONBLOCK, 0, &mq_attr);
    if (camera_mq == -1) {
        perror("mq_open - camera_mq - error");
        return -1;
    }
    compression_mq = mq_open(COMPRESSION_MQ, O_CREAT|O_RDWR|O_NONBLOCK, 0, &mq_attr);
    if (compression_mq == -1) {
        perror("mq_open - compression_mq - error");
        return -1;
    }
    return 0;
}

/**
- Calculates the difference between the camera and computer times to accurately embed the
computer time into each PPM header.
- Captures frames from the desired video stream and saves them to buffered_frames in JPEG format at a maximum rate of the desired frame rate.
- If the user desired verbosity, then the frames captured per second will be printed to standard output.
- The rate of iteration is controlled by the sequencer thread via camera_sem (semaphore).
- If only 1 thread is running and save images is requested, then this function will save the images in JPEG format on a remote machine.
- If at least 2 threads are running, then the pointers to the saved frames are passed via message queue to be converted to PPM format.
- When the total desired frames is reached, the function will exit.
- If the message queue is full, a pointer will be popped and the last pointer will be pushed, and this function will exit.
- Upon exiting, the last pointer will be pushed to the message queue with a code to the next thread to exit.
.
*/
void *acquire_frames(void *args) {
    int rc = 1; // stores return codes
    unsigned int frames = 0; // Counts frames for recording frames per second.
    unsigned int total_frames = 0;   // Counts total frames recorded.
    struct timespec start_time, current_time, diff_time;    // Manage time
    char temp_string[STRING_SIZE];   // handle all strings
    unsigned int index;

    // Wait for a frame to be stored in memory.
    do {
        rc = poll(&fds, 1, 10000);
        if (rc == -1) {
            perror("poll error");
        } else if (rc == 0) {
            perror("poll timeout - init time");
        }
    } while (rc == 0);
    // Dequeue the buffered memory.
    rc = xioctl(fds.fd, VIDIOC_DQBUF, &buf);
    if (rc == -1) {
        perror("VIDIOC_DQBUF");
        pthread_exit((void *)-1);
    }

    // Store computer time and camera time
    rc = clock_gettime(CLOCK_REALTIME, &(initial_time.computer));
    if (rc != 0) {
        perror("clock_gettime error");
        pthread_exit((void *)-1);
    }
    TIMEVAL_TO_TIMESPEC(&buf.timestamp, &initial_time.camera);

    // If verbose, save start time to count frames every second.
    if (verbose) {
        start_time.tv_sec = initial_time.computer.tv_sec;
        start_time.tv_nsec = initial_time.computer.tv_nsec;
    }

    // Wait for scheduler to increment semaphore.
    rc = sem_wait(&camera_sem);
    if (rc != 0) {
        perror("sem_wait - camera_sem - error");
        pthread_exit((void *)-1);
    }

    // Iterate through the streamed frames
    while(1) {
        // If frame timestamp exceeds fps period, then save frame to a buffer.
        if (timeval_to_long_long(&buf.timestamp) >= timespec_to_long_long(&initial_time.camera) + (long long)(((double)total_frames/fps)*NSEC_IN_SEC)) {
            index = total_frames % frame_buffer_size;
            // Save frame timestamp
            TIMEVAL_TO_TIMESPEC(&buf.timestamp, &buffered_frame[index].timestamp);
            // Save frame
            memcpy((void *)buffered_frame[index].frame[0].data, buffered_camera[buf.index].start, buf.bytesused);
            buffered_frame[index].frame[0].size = buf.bytesused;
            // If only one thread is running
            if (num_threads <= 1) {
                // Write frame to file on remote machine via network transfer
                if (write_file) {
                    save_image(index, total_frames);
                }
            // If more than one thread is running, send total_frames to embed_ppm thread via message queue
            } else {
                rc = sprintf(temp_string, "%d", total_frames);
                if (rc < 0){
                    perror("sprintf - acquire_frames - error");
                }
                rc = mq_send(camera_mq, temp_string, MSG_SIZE, 30);
                if (rc == -1 && errno == EAGAIN) {
                    rc = mq_receive(camera_mq, temp_string, MSG_SIZE, NULL);
                    rc = sprintf(temp_string, "%d", max_frames-1);
                    if (rc < 0){
                        perror("sprintf - acquire_frames - error");
                    }
                    rc = mq_send(camera_mq, temp_string, MSG_SIZE, 30);
                    printf("Camera_mq full. Exiting.\n");
                    pthread_exit((void *)-1);
                }
                if (rc == -1){
                    perror("camera_mq_send error");
                }
            }
            if (verbose) frames++;   // Count frames recorded for this second
            total_frames++; // Count total frames recorded
        }

        // If maximum desired frames is reached, break from loop.
        if (total_frames == max_frames) {
            printf("Max frames read.\n");
            pthread_exit((void *)0);
        }

        // Once per second, increment start_time by one second, and if verbosity
        // is set, print frames per second to standard output.
        // Acquire stop timer for fps report
        if (verbose) {
            rc = clock_gettime(CLOCK_REALTIME, &current_time);
            if (rc != 0) {
                perror("clock_gettime error");
                pthread_exit((void *)-1);
            }
            time_diff(&start_time, &current_time, &diff_time);
            if (timespec_to_long_long(&diff_time) >= NSEC_IN_SEC) {
                start_time.tv_sec++;
                printf("%d total frames : %d fps\n", total_frames, frames);
                frames = 0;
            }
        }

        // Queue memory buffer into video streaming queue
        rc = xioctl(fds.fd, VIDIOC_QBUF, &buf);
        if (rc == -1) {
            perror("VIDIOC_QBUF");
            return (void *)1;
        }


        do {
            // Wait for sequencer to increment semaphore
            rc = sem_wait(&camera_sem);
            if (rc != 0) {
                perror("sem_wait - camera_sem - error");
                pthread_exit((void *)-1);
            }
            // Non-blocking poll for memory buffer with frame.
            rc = poll(&fds, 1, 0);
            if (rc == -1) {
                perror("poll error");
            }
        // If no frame is stored in memory, wait for sequencer to decrement semaphore
        } while (rc == 0);

        // Remove buffered memory with frame from video streaming queue.
        rc = xioctl(fds.fd, VIDIOC_DQBUF, &buf);
        if (rc == -1) {
            perror("VIDIOC_QBUF");
            return (void *)1;
        }
    }
    pthread_exit((void *)0);
}

/**
- Iterates at a frequency set my the sequencer.
- If a pointer to a frame is in the incoming message queue, then pop the message queue.
- Decodes the JPEG into a PPM image.
- Embeds the image's timestamp and the system's information into the PPM header.
- If 2 threads are running and save images is requested, then this function will save the images in PPM format on a remote machine.
- If 3 threads are running, then the pointers to the saved frames are passed via the outgoing message queue to be compressed to a zlib format.
- If a message to exit is popped from the incoming message queue, this function will process the image as normal and exit.
- If the message queue is full, a pointer will be popped and the last pointer will be pushed, and this function will exit.
- Upon exiting, the last pointer will be pushed to the outgoing message queue with a code to the next thread to exit.
.
*/
void *embed_ppm_data(void *args) {
    int rc = 1; // variable for storing return codes
    char mq_msg[MSG_SIZE];
    int mq_msg_int = 0;
    int index;
    int jpegSubsamp = 0;
    tjhandle _jpegDecompressor = tjInitDecompress();

    // Wait for scheduler to increment semaphore
    rc = sem_wait(&embed_ppm_sem);
    if (rc != 0) {
        perror("sem_wait - embed_ppm_sem - error");
        pthread_exit((void *)-1);
    }

    // Iterate forever receiving filenames from the message queue to insert
    // header data into the files.
    while (1) {

        // Read frame index from the message queue
        rc = mq_receive(camera_mq, mq_msg, MSG_SIZE, NULL);
        // If message queue is empty, wait for sequencer to increment semaphore
        if (rc == -1 && errno == EAGAIN) {
            rc = sem_wait(&embed_ppm_sem);
            if (rc != 0) {
                perror("sem_wait - embed_ppm_sem - error");
                pthread_exit((void *)-1);
            }
            continue;
        } else if (rc == -1) {
            perror("mq_receive - camera_mq - error");
            // Wait for sequencer to increment semaphore
            rc = sem_wait(&embed_ppm_sem);
            if (rc != 0) {
                perror("sem_wait - embed_ppm_sem - error");
                pthread_exit((void *)-1);
            }
            continue;
        } else {
            // Decode JPEG to PPM
            mq_msg_int = atoi(mq_msg);
            index = mq_msg_int % frame_buffer_size;
            buffered_frame[index].frame[1].size = sizeof(unsigned char)*3*hres*vres;
            tjDecompressHeader2(_jpegDecompressor, buffered_frame[index].frame[0].data, buffered_frame[index].frame[0].size, &hres, &vres, &jpegSubsamp);
            tjDecompress2(_jpegDecompressor, buffered_frame[index].frame[0].data, buffered_frame[index].frame[0].size, buffered_frame[index].frame[1].data, hres, 0, vres, TJPF_RGB, TJFLAG_FASTDCT);
            // Embed image's timestamp and system's information into image header
            embed_ppm_header(index);
            // If only 2 threads running
            if (num_threads <= 2) {
                // Write frame to file on remote machine via network transfer
                if (write_file) {
                    save_image(index, mq_msg_int);
                }
            // If more than 2 threads running
            } else {
                // Send pointer to image to compression thread via message queue
                compression_mq_size++;
                rc = mq_send(compression_mq, mq_msg, MSG_SIZE, 30);
                // If message queue is full, pop queue, push image to queue with code to exit, then exit
                if (rc == -1 && errno == EAGAIN) {
                    rc = mq_receive(compression_mq, mq_msg, MSG_SIZE, NULL);
                    rc = sprintf(mq_msg, "%d", max_frames-1);
                    if (rc < 0){
                        perror("sprintf - acquire_frames - error");
                    }
                    rc = mq_send(compression_mq, mq_msg, MSG_SIZE, 30);
                    printf("Compression_mq full. Exiting.\n");
                    pthread_exit((void *)-1);
                }
                if (rc == -1){
                    perror("compression_mq_send error");
                }
            }
            // If maximum desired frames is reached, break from loop and exit
            if (mq_msg_int == max_frames-1) {
                tjDestroy(_jpegDecompressor);
                printf("Header embedded into all frames.\n");
                pthread_exit((void *)0);
            }
        }
    }
}

/**
- Iterates at a frequency set my the sequencer.
- If a pointer to a frame is in the incoming message queue, then pop the message queue.
- Compressed the PPM into zlib format.
- If a message to exit is popped from the incoming message queue, this function will process the image as normal and exit.
.
*/
void *compress_frames(void *args) {
    int rc; // variable for storing return codes
    int mq_msg_int = 0;
    int index;
    char mq_msg[MSG_SIZE];
    int compression_level;

    // Wait for scheduler to increment semaphore
    rc = sem_wait(&compression_sem);
    if (rc != 0) {
        perror("sem_wait - compression_sem - error");
        pthread_exit((void *)-1);
    }

    // Iterate forever
    while (1) {
        // Attempt to pop pointer from message queue
        rc = mq_receive(compression_mq, mq_msg, MSG_SIZE, NULL);
        // If message queue is empty, wait for sequencer
        if (rc == -1 && errno == EAGAIN) {
            rc = sem_wait(&compression_sem);
            if (rc != 0) {
                perror("sem_wait - compression_sem - error");
                pthread_exit((void *)-1);
            }
            continue;
        } else if (rc == -1) {
            perror("mq_receive - compression - error");
            // Wait for sequencer to increment semaphore
            rc = sem_wait(&compression_sem);
            if (rc != 0) {
                perror("sem_wait - compression_sem - error");
                pthread_exit((void *)-1);
            }
            continue;
        } else {
            // Compress PPM image into zlib format.
            compression_mq_size--;
            mq_msg_int = atoi(mq_msg);
            index = mq_msg_int % frame_buffer_size;
            compression_level = 9.0*(1.0 - (float)compression_mq_size/mq_attr.mq_maxmsg);
            buffered_frame[index].frame[2].size = compressBound(buffered_frame[index].frame[1].size);
            rc = compress2((Bytef *)buffered_frame[index].frame[2].data, (uLongf *)(&buffered_frame[index].frame[2].size), (const Bytef *)buffered_frame[index].frame[1].data, (uLong)buffered_frame[index].frame[1].size, compression_level);
            if (rc == Z_BUF_ERROR) printf("Z_BUF_ERROR error\n");
            if (res == Z_MEM_ERROR) printf("Z_MEM_ERROR error\n");
            // Write frame to file on remote machine via network transfer
            if (write_file) {
                save_image(index, mq_msg_int);
            }
            // If maximum desired frames is reached, break from loop.
            if (mq_msg_int == max_frames-1) {
                printf("Compressed all frames.\n");
                pthread_exit((void *)0);
            }
        }
    }
}

/**
Control iteration rates of other threads through use of semaphores. Sequencer
iterates at twice the desired frame rate. Camera iterates at the same rate,
embed_ppm ppm iterates at 1/4 the rate, and compression iterates at 1/8 the
rate. Between iteration, sequencer sleeps. Values were determined through
estimating average case execution times and following rate-monotonic
policy. Average cases were used rather than worst cases because this is a
soft real-time system and worst cases were too long to achieve 1 Hz frequency
on resource restricted hardware. Harmonic values were selected to maximize
CPU usage.
*/
void *sequencer(void *args) {

    int rc;
    struct timespec start_time, current_time, diff_time, sleep_time;
    int iteration = 0;
    long long nanoseconds;
    float sequencer_frequency = fps/0.5;
    float camera_frequency = fps/0.5;
    float embed_ppm_frequency = fps/2.0;
    float compression_frequency = fps/4.0;

    // Store time at beginning of loop
    rc = clock_gettime(CLOCK_REALTIME, &start_time);
    if (rc != 0) {
        perror("clock_gettime error.\n");
        if (errno == EFAULT) printf("EFAULT.\n");
        else if (errno == EINVAL) printf("EINVAL.\n");
        else if (errno == EPERM) printf("EPERM.\n");
        else printf("errno: %d\n", errno);
        pthread_exit((void *)-1);
    }

    // Iterate forever
    while (1) {

        if (iteration % (int)(sequencer_frequency/camera_frequency) == 0) {
            // Increment camera semaphore
            rc = sem_post(&camera_sem);
            if (rc == -1) {
                perror("sem_post - camera_sem - error");
                pthread_exit((void *)-1);
            }
        }
        if (iteration % (int)(sequencer_frequency/embed_ppm_frequency) == 0) {
            // Increment embed_ppm semaphore
            rc = sem_post(&embed_ppm_sem);
            if (rc == -1) {
                perror("sem_post - embed_ppm - error");
                pthread_exit((void *)-1);
            }
        }
        if (iteration % (int)(sequencer_frequency/compression_frequency) == 0) {
            // Increment compression semaphore
            rc = sem_post(&compression_sem);
            if (rc == -1) {
                perror("sem_post - compression_sem - error");
                pthread_exit((void *)-1);
            }
        }

        // Incremented iteration counter
        iteration++;

        // Get current time
        rc = clock_gettime(CLOCK_REALTIME, &current_time);
        if (rc != 0) {
            perror("clock_gettime error");
            pthread_exit((void *)-1);
        }
        // Calculate time remaining in iteration period is sleep for remainder.
        time_diff(&start_time, &current_time, &diff_time);
        if (timespec_to_long_long(&diff_time) < (1.0/sequencer_frequency)*(iteration%(int)sequencer_frequency==0?((int)sequencer_frequency):iteration%(int)sequencer_frequency)*NSEC_IN_SEC) {
            nanoseconds = (1.0/sequencer_frequency)*(iteration%(int)sequencer_frequency==0?((int)sequencer_frequency):iteration%(int)sequencer_frequency)*NSEC_IN_SEC - timespec_to_long_long(&diff_time);
            long_long_to_timespec(&sleep_time, nanoseconds);
            rc = nanosleep(&sleep_time, NULL);
            if (rc == -1) {
                perror("nanosleep error");
            }
        }

        // Get current time
        rc = clock_gettime(CLOCK_REALTIME, &current_time);
        if (rc != 0) {
            perror("clock_gettime error");
            pthread_exit((void *)-1);
        }
        // If time time difference between start time and current exceeds 1 second
        // then increment start time by 1 second
        time_diff(&start_time, &current_time, &diff_time);
        if (timespec_to_long_long(&diff_time) >= NSEC_IN_SEC) {
            start_time.tv_sec++;
            // If compression thread has been called at least once, then reset iteration counter
            if (iteration == sequencer_frequency/compression_frequency) iteration = 0;
        }
    }
    pthread_exit((void *)0);
}

/**
Initialize and start all desired threads, then wait for threads to exit.
*/
int create_threads() {
    int rc = 1; // variable for storing return codes
    pthread_t sequencer_thread, camera_thread, ppm_data_thread, compression_thread;   // threads
    pthread_attr_t thread_attr; // thread attributes
    struct sched_param thread_param;    // thread scheduler parameters
    // cpu_set_t mask; // cpu affinity mask

    // Store the maximum priority for the FIFO scheduler.
    int fifo_max_prio = sched_get_priority_max(SCHED_FIFO);
    if (fifo_max_prio == -1) {
        perror("sched_get_priority_max error");
        return -1;
    }

    // Initialize a pthread attribute structure for all threads
    rc = pthread_attr_init(&thread_attr);
    if (rc != 0) {
        perror("pthread_attr_init error");
        return -1;
    }
    // Ensure pthreads attributes must be explicitly set.
    rc = pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);
    if (rc != 0) {
        perror("pthread_attr_setinheritsched error");
        return -1;
    }
    // Set pthread scheduler to FIFO.
    rc = pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    if (rc != 0) {
        perror("pthread_attr_setschedpolicy error");
        return -1;
    }

    // Set priority to maximum-3 and create thread for compressing frames
    if (num_threads >= 3) {
        // Assign cpu mask to CPU 0
        // CPU_ZERO(&mask);
        // CPU_SET(0, &mask);
        // rc = pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &mask);
        // if (rc != 0) {
        //     perror("pthread_attr_setaffinity_np error");
        //     return -1;
        // }
        thread_param.sched_priority = fifo_max_prio - 3;
        rc = pthread_attr_setschedparam(&thread_attr, &thread_param);
        if (rc != 0) {
            perror("pthread_attr_setschedparam error");
            return -1;
        }
        rc = pthread_create(&compression_thread, &thread_attr, compress_frames, NULL);
        if (rc != 0) {
            perror("pthread_create - compression - error");
            return -1;
        }
    }

    // Set priority to maximum-2 and create thread for decoding JPEG to PPM and
    // embedding frame timestamp and system data to PPM header
    if (num_threads >= 2) {
        // Assign cpu mask to CPU 1
        // CPU_ZERO(&mask);
        // CPU_SET(1, &mask);
        // rc = pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &mask);
        // if (rc != 0) {
        //     perror("pthread_attr_setaffinity_np error");
        //     return -1;
        // }
        thread_param.sched_priority = fifo_max_prio - 2;
        rc = pthread_attr_setschedparam(&thread_attr, &thread_param);
        if (rc != 0) {
            perror("pthread_attr_setschedparam error");
            return -1;
        }
        rc = pthread_create(&ppm_data_thread, &thread_attr, embed_ppm_data, NULL);
        if (rc != 0) {
            perror("pthread_create - ppm_data - error");
            return -1;
        }
    }

    // Set priority to maximum-1 and create thread for capturing frames from
    // video stream.
    if (num_threads >= 1) {
        // Assign cpu mask to CPU 2
        // CPU_ZERO(&mask);
        // CPU_SET(1, &mask);
        // rc = pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &mask);
        // if (rc != 0) {
        //     perror("pthread_attr_setaffinity_np error");
        //     return -1;
        // }
        thread_param.sched_priority = fifo_max_prio-1;
        rc = pthread_attr_setschedparam(&thread_attr, &thread_param);
        if (rc != 0) {
            perror("pthread_attr_setschedparam error");
            return -1;
        }
        rc = pthread_create(&camera_thread, &thread_attr, acquire_frames, NULL);
        if (rc != 0) {
            perror("pthread_create - camera - error");
            return -1;
        }
    }

    //Create scheduler thread with maximum priority.
    if (num_threads >= 1) {
        // Assign cpu mask to CPU 3
        // CPU_ZERO(&mask);
        // CPU_SET(3, &mask);
        // rc = pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &mask);
        // if (rc != 0) {
        //     perror("pthread_attr_setaffinity_np error");
        //     return -1;
        // }
        thread_param.sched_priority = fifo_max_prio;
        rc = pthread_attr_setschedparam(&thread_attr, &thread_param);
        if (rc != 0) {
            perror("pthread_attr_setschedparam error");
            return -1;
        }
        rc = pthread_create(&sequencer_thread, &thread_attr, sequencer, NULL);
        if (rc != 0) {
            perror("pthread_create - sequencer - error");
            return -1;
        }
    }

    // Wait for pthreads to exit.
    if (num_threads >= 1) {
        rc = pthread_join(camera_thread, NULL);
        if (rc != 0) {
            perror("pthread_join - camera - error");
            return -1;
        }
    }
    if (num_threads >= 2) {
        rc = pthread_join(ppm_data_thread, NULL);
        if (rc != 0) {
            perror("pthread_join - ppm_data - error");
            return -1;
        }
    }
    if (num_threads >= 3) {
        rc = pthread_join(compression_thread, NULL);
        if (rc != 0) {
            perror("pthread_join - compression - error");
            return -1;
        }
    }

    return 0;
}


/**
Release all global objects and variables.
- Close and destroy message queues.
- Destroy the semaphores.
- Command device to stop streaming.
- Unset all memory-mapped buffers.
- Destory libcurl objects.
- Free memory for buffered_frame.
- Free memory for buffered_camera.
.
*/
void *finish() {
    int rc = 0; // Store return codes

    // Close and destroy message queues
    rc = mq_close(camera_mq);
    if (rc == -1) {
        perror("mq_close - camera_mq - error");
    }
    rc = mq_unlink(CAMERA_MQ);
    if (rc == -1) {
        perror("mq_unlink - CAMERA_MQ - error");
    }
    rc = mq_close(compression_mq);
    if (rc == -1) {
        perror("mq_close - compression_mq - error");
    }
    rc = mq_unlink(COMPRESSION_MQ);
    if (rc == -1) {
        perror("mq_unlink - COMPRESSION_MQ - error");
    }

    // Destroy the semaphores
    rc = sem_destroy(&camera_sem);
    if (rc == -1) {
        perror("sem_destory - camera_sem - error");
    }
    rc = sem_destroy(&embed_ppm_sem);
    if (camera_mq == -1) {
        perror("sem_destory - embed_ppm_sem - error");
    }
    rc = sem_destroy(&compression_sem);
    if (rc == -1) {
        perror("sem_destory - compression_sem - error");
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

    // Destroy libcurl objects.
    curl_easy_cleanup(curl);

    // Free memory for buffered_frame
    for (int i = 0; i < frame_buffer_size; i++) {
        for (int j = 0; j < num_threads; j++) {
            free(buffered_frame[i].frame[j].data);
        }
        free(buffered_frame[i].frame);
    }
    free(buffered_frame);

    // Free memory for buffered_camera
    free(buffered_camera);

    return 0;
}

/**
Call accept_options(), initialize(), create_threads(), finish(), then exit.
If either accept_options() or initialize() return -1, then exit.
*/
int main(int argc, char** argv) {
    if (accept_options(argc, argv) == -1) exit(EXIT_FAILURE);
    if (initialize() == -1) exit(EXIT_FAILURE);
    create_threads();
    finish();
    exit(EXIT_SUCCESS);
};
