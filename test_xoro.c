/* Basic test for reading from /dev/xoroshiro128p */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


int main(int argc, char *argv[])
{
    uint64_t *times = malloc(sizeof(uint64_t) * 8);
    int fd = open("/dev/xoroshiro128p", O_RDWR);
    if (0 > fd) {
        perror("Failed to open the device.");
        free(times);
        return errno;
    }

    /* Test reading different numbers of bytes */
    for (int i = 0; i < 10; i++) {
        ssize_t n_bytes_read = read(fd, times, 8);

        if (0 > n_bytes_read) {
            perror("Failed to read all bytes.");
            free(times);
            return errno;
        }

        printf("%d %lu %lu %lu %lu %lu %lu %lu %lu\n", i, times[0], times[1],
               times[2], times[3], times[4], times[5], times[6], times[7]);
    }

    free(times);
    return 0;
}
