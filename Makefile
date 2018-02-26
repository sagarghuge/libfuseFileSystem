COMPILER = gcc
SOURCE = onflyfs.c

build: $(SOURCE)
	$(COMPILER) $(SOURCE) -o onflyfs `pkg-config fuse --cflags --libs`
	echo 'To Mount: ./onflyfs rootDir MountPoint'

clean:
	rm onflyfs 
