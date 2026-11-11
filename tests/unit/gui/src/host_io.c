/* Runner side (host libc): file output. */
#include <stdio.h>

int g1_gui_host_write(const char *path, const void *data, unsigned long len)
{
	FILE *f = fopen(path, "wb");
	size_t n;

	if (f == NULL) {
		return -1;
	}
	n = fwrite(data, 1, len, f);
	if (fclose(f) != 0 || n != len) {
		return -1;
	}
	return 0;
}
