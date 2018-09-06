// This program demonstrates decoding a JPEG image into a RGB image using
// turbo-jpeg.


#include <stdio.h>
#include <errno.h>
#include <turbojpeg.h>
#include <string.h>



int main(int argc, char** argv) {
    int rc;
    int width = 640;
    int height = 480;
    char header[128];
    char filename[128];
    unsigned char jpeg[width*height*3];
    unsigned char rgb[width*height*3+128];
    FILE *file;
    int jpegSubsamp = 0;

    // Initalize the tjhandle for decompression.
    tjhandle _jpegDecompressor = tjInitDecompress();

    // We'll load a JPEG file from storage to decode.
    file = fopen("../frames/jpeg/frame_init.jpeg", "rb");
    rc = fread(jpeg, sizeof(unsigned char), 640*480*3, file);
    fclose(file);

    // Gather header information from file.
    tjDecompressHeader2(_jpegDecompressor, jpeg, rc, &width, &height, &jpegSubsamp);
    // Decode JPEG into RGB.
    // There are other options than TJPF_RGB and TJFLAG_FASTDCT. These 2 are
    // common but refer to the jpeg-turbo documentation for other options.
    tjDecompress2(_jpegDecompressor, jpeg, rc, rgb, width, 0, height, TJPF_RGB, TJFLAG_FASTDCT);

    // And that's it. An RGB image is saved in rgb. I'm not sure if there's a
    // way to view the image in its current form, so we'll add a PPM header
    // and save it as a .ppm file. This is still an RGB file, just represented
    // as a PPM file, specifically P6 since its in binary.

    // I haven't looked into it, but jpeg-turbo may offer an option to convert
    // to P3 (ascii RGB). It's fun for learning, but I've found working with P3
    // to run much more slowly than P6.

    // This adds "P6\n640 480\n255\n" to the top of the file.
    // It tells the computer it is a P6 file (or binary PPM), has 640x480
    // resolution, and 8-bit (255) color.
    sprintf(header, "P6\n%d %d\n255\n", width, height);
    memmove((void *)rgb + sizeof(char)*strlen(header), (void *)rgb, height*width*3);
    memmove((void *)rgb, (void *)header, sizeof(char)*strlen(header));

    // Save the file so we can view it. Feel free to view the image in a text
    // editor.
    file = fopen("../frames/ppm/frame_init.ppm", "wb");
    rc = fwrite(rgb, sizeof(unsigned char), 640*480*3, file);
    fclose(file);


    // That was pretty simple.
    // Let's loop through 10 more images just for fun.
    for (int i = 0; i < 10; i++) {
        sprintf(filename, "../frames/jpeg/frame_%04d.jpeg", i);
        file = fopen(filename, "rb");
        rc = fread(jpeg, sizeof(unsigned char), 640*480*3, file);
        fclose(file);

        tjDecompressHeader2(_jpegDecompressor, jpeg, rc, &width, &height, &jpegSubsamp);
        tjDecompress2(_jpegDecompressor, jpeg, rc, rgb, width, 0, height, TJPF_RGB, TJFLAG_FASTDCT);

        sprintf(header, "P6\n%d %d\n255\n", width, height);
        memmove((void *)rgb + sizeof(char)*strlen(header), (void *)rgb, height*width*3);
        memmove((void *)rgb, (void *)header, sizeof(char)*strlen(header));

        sprintf(filename, "../frames/ppm/frame_%04d.ppm", i);
        file = fopen(filename, "wb");
        rc = fwrite(rgb, sizeof(unsigned char), 640*480*3, file);
        fclose(file);
    }

    // Destroy the tjhandle.
    tjDestroy(_jpegDecompressor);
}
