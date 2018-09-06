
for filename in ./frames/zlib/frame_*.zlib; do
    zlib_file="${filename}"
    ppm_file="${zlib_file%.*}"
    (zlib-flate -uncompress < "${zlib_file}") > "${ppm_file}"
    rm "${zlib_file}"
done
