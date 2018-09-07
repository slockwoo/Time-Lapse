// This program demonstrates compressing an rgb file into a zlib file using
// the zlib library.


#include <stdio.h>
#include <zlib.h>

// Verify the resolution of your images.
#define WIDTH 640
#define HEIGHT 480


int main(int argc, char** argv) {
    int rc;
    FILE *file;
    int width = WIDTH;
    int height = HEIGHT;
    char filename[128];
    unsigned char rgb[width*height*3+128];
    unsigned char zlib[width*height*3+128];
    int size;

    // We'll open an RGB (ppm) file from storage to compress.
    file = fopen("../frames/ppm/frame_init.ppm", "rb");
    rc = fread(rgb, sizeof(unsigned char), width*height*3+128, file);
    fclose(file);

    // This returns an upper bound for the size of the compressed image in bytes
    size = compressBound(rc);
    // This performs the actual compression.
    // The last number is the compression level. It can be anywhere from 0-9
    // where 0 is no compression and 9 is maximum compression. The higher the
    // compression, the smaller the zlib image, but the longer it takes.
    compress2((Bytef *)zlib, (uLongf *)(&size), (const Bytef *)rgb, (uLong)(sizeof(unsigned char)*width*height*3+128), 1);

    // And that's it. We compressed an RGB into a zlib file.

    // Let's write the zlib image to a file.
    // I don't know what the standard naming convention is but appending .zlib
    // to the end of the file seems to do the trick for me.
    file = fopen("../frames/zlib/frame_init.ppm.zlib", "wb");
    fwrite(zlib, sizeof(unsigned char), size, file);
    fclose(file);

    // I included a decompression script called 'decompress.sh'. It will
    // decompress all .zlib files in the /frames/zlib/ directory.

    // Now that the simple zlib compression exampe is over, let's compress
    // 10 more files.
    for (int i = 0; i < 10; i++) {

        // THIS WTF!!!??? I don't know why, but height is getting changed to 0
        // after every iteration on my machine. It's never an lvalue and its
        // pointer is never passed anywhere. Maybe I'll figure out this error
        // in the future....., or maybe I'll never look at this code again and
        // forget all about it.
        height = 480;

        sprintf(filename, "../frames/ppm/frame_%04d.ppm", i);
        file = fopen(filename, "rb");
        rc = fread(rgb, sizeof(unsigned char), width*height*3+128, file);
        fclose(file);

        size = compressBound(rc);
        compress2((Bytef *)zlib, (uLongf *)(&size), (const Bytef *)rgb, (uLong)(sizeof(unsigned char)*width*height*3+128), 9);

        sprintf(filename, "../frames/zlib/frame_%04d.ppm.zlib", i);
        file = fopen(filename, "wb");
        fwrite(zlib, sizeof(unsigned char), size, file);
        fclose(file);
    }


    return 0;
}
