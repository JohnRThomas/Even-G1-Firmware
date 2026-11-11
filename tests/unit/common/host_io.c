/* Runs in the native_sim runner (host libc): writes a file for a test, 0 on success. */
#include <stdio.h>

int g1_host_write_file(const char *path, const void *data, unsigned long len)
{
  FILE *file = fopen(path, "wb");
  size_t written;

  if (file == NULL) {
    return -1;
  }
  written = fwrite(data, 1, len, file);
  if (fclose(file) != 0 || written != len) {
    return -1;
  }
  return 0;
}
