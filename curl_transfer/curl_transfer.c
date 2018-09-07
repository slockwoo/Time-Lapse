// This program uses the FTP protocol.
// To enable FTP on my machine (Ubuntu)
//      sudo apt install vsftpd
//      sudo vim /etc/vsftpd.conf
//      // Uncomment write_enable=YES // line 31 on my machine
//      sudo service vsftpd restart

// This program demonstrates transferring a file using FTP to a remote machine
// using libcurl.

// TARGET_ADDRESS and CREDENTIALS will need to be changed to work with the
// remote computer. The same computer can be used for initial testing.


#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Verify the resolution of your images.
#define WIDTH 640
#define HEIGHT 480

// TARGET_ADDRESS and CREDENTIALS will need to be changed to work with the
// remote computer. The same computer can be used for initial testing.
#define TARGET_ADDRESS "192.168.137.2/Documents/ECEN5623/stream_mjpeg/frames/curl/"
#define CREDENTIALS "username:password"

//Stores the location and length of an image.
struct Image {
    unsigned char *data;    // Pointer to stored image.
    size_t size;    // Length in bytes of stored image.
};


/* Replaces libcurl's read_callback function.
Iteratively copies an image to *ptr to allow libcurl to transmit the image
via network to ultimately be saved on the target machine.
The entire image can't be transferred in a single packet so this function
will be called several times by libcurl per image.
The numbers of bytes transferred is returned. If 0 is returned, libcurl stops
caling this function.
*/
static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp) {
    struct Image *upload = (struct Image *)userp;
    size_t copylen;
    size_t max;

    // Set maximum bytes which can be transferred in a single packet
    max = size*nmemb;

    if (upload->size < 1) return 0;

    // If bytes remaining is less than maximum packet size, make packet size
    // smaller.
    if (max > upload->size) copylen = upload->size;
    else copylen = max;

    // Transfer data from image to ptr so libcurl can transfer the data.
    memcpy(ptr, upload->data, copylen);

    // Shift image pointer location and image size to beginning of remaining
    // of unsent image.
    upload->data += copylen;
    upload->size -= copylen;

    // Returns number of bytes sent.
    return copylen;
}


int main(int argc, char** argv) {
    int temp_size;
    int width = WIDTH;
    int height = HEIGHT;
    char filename[128];
    struct Image image;
    FILE *file;
    CURL *curl;

    // Allocate memory to store the image.
    image.data = (unsigned char *)malloc(sizeof(unsigned char)*width*height*3+128);

    // Setup libcurl to transfer images over network.
    curl_global_init(CURL_GLOBAL_DEFAULT);
    // Initialize the curl struct to default values
    curl = curl_easy_init();
    // Setup username and password is necessary
    curl_easy_setopt(curl, CURLOPT_USERPWD, CREDENTIALS);
    // Setup libcurl for uploading. 1L is a standard value.
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    // Setup custom read_callback function.
    // libcurl offers a default read_callback function if desired.
    // I've never used it so can't comment on it.
    // Supposedly its works similarly to fwrite???
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);

    // We'll load an image from storage.
    // libcurl only views the files as 1's and 0's so the file chosen isn't
    // important.
    file = fopen("../frames/jpeg/frame_init.jpeg", "rb");
    image.size = fread(image.data, sizeof(unsigned char), height*width*3+128, file);
    fclose(file);

    // The pointer to the image is changed during the read_callback so we'll
    // store it here to reset the pointer when we're done.
    // This is useful if we wan't do perform other operations with the data.
    // We dynamically allocated the memory so we'll need the pointer to free
    // the memory when we're done.
    temp_size = image.size;

    // Setup the location to send and save the file.
    sprintf(filename, "ftp://%sframe_init.jpeg", TARGET_ADDRESS);
    curl_easy_setopt(curl, CURLOPT_URL, filename);
    // Send the data and its size in bytes to the read_callback function.
    curl_easy_setopt(curl, CURLOPT_READDATA, &image);
    // Send the size of the image in bytes to libcurl.
    curl_easy_setopt(curl, CURLOPT_INFILESIZE, (curl_off_t)image.size);
    // This tells libcurl everything is setup and to begin transferring the
    // data. libcurl will beginning calling read_callback iteratively and the
    // file should be saved in its destination when this returns.
    curl_easy_perform(curl);

    // Reset image pointer to start of image
    image.data -= temp_size;
    // Reset image size to size of image
    image.size = temp_size;


    // Now that we've shown how to transfer a single file, let's tranfer 10
    // more in a loop.
    for (int i = 0; i < 10; i++) {
        sprintf(filename, "../frames/jpeg/frame_%04d.jpeg", i);
        file = fopen(filename, "rb");
        image.size = fread(image.data, sizeof(unsigned char), height*width*3+128, file);
        fclose(file);

        temp_size = image.size;
        sprintf(filename, "ftp://%sframe_%04d.jpeg", TARGET_ADDRESS, i);
        curl_easy_setopt(curl, CURLOPT_URL, filename);
        curl_easy_setopt(curl, CURLOPT_READDATA, &image);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE, (curl_off_t)image.size);
        curl_easy_perform(curl);

        image.data -= temp_size;
        image.size = temp_size;
    }


    // Destroy libcurl objects.
    curl_easy_cleanup(curl);
    // Free allocated memory for image
    free(image.data);

    return 0;
}
