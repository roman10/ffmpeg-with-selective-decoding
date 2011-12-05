#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    int fd, offset;
    int *data;
    struct stat sbuf;
	FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: mmapdemo offset\n");
        exit(1);
    }

	//f = fopen("./h1_1280_720_5m.mp4_mbstpos_gop1.txt", "w");
	//fprintf(f, "sssssssssssssssssssssssssss");
	//fflush(f);
	//this line will cause no such device error
	//printf("fd: %d\n", fd);
	//if (fd = open("./h1_1280_720_5m.mp4_mbstpos_gop1.txt", O_RDONLY) == -1) {			//line caused bug
    if ((fd = open("./h1_1280_720_5m.mp4_mbstpos_gop1.txt", O_RDONLY)) == -1) {			//correct source code
        perror("open");
        exit(1);
    }
	//printf("fd: %d\n", fd);
    if (stat("./h1_1280_720_5m.mp4_mbstpos_gop1.txt", &sbuf) == -1) {
        perror("stat");
        exit(1);
    }
	printf("file size: %ld\n", sbuf.st_size);
    offset = atoi(argv[1]);
    if (offset < 0 || offset > sbuf.st_size/4-1) {
        fprintf(stderr, "mmapdemo: offset must be in the range 0-%ld\n", (sbuf.st_size)/4 - 1);
        exit(1);
    }
    
    data = mmap(0, sbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)	;
    if (data == (-1)) {
        perror("mmap");
        exit(1);
    }
	
    printf("byte at offset %d is %d\n", offset, data[offset]);

    return 0;
}
