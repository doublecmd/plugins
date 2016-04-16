# compiler options
CC = gcc
CFLAGS =-I. -I/usr/include -I../../sdk \
	-Wall -fPIC -O2 -g -std=c99 \
	-DG_DISABLE_DEPRECATED -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

VFS_OBJECTS=exif.o

.SUFFIXES: .c
.c.o:
	$(CC) $(CFLAGS) -c $<

all: exif.wdx

exif.wdx: $(VFS_OBJECTS)
	$(CC) -shared -o exif.wdx $(VFS_OBJECTS) $(CFLAGS)

clean: 
	rm -f *.o *.d *.gch
	rm -f exif.wdx
