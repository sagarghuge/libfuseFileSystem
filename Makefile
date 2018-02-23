COMPILER = gcc
SOURCE = simpleFileSystem.c

build: $(SOURCE)
	$(COMPILER) $(SOURCE) -o simpleFileSystem `pkg-config fuse --cflags --libs`
	echo 'To Mount: ./simpleFileSystem -f [mount point]'

clean:
	rm simpleFileSystem 
