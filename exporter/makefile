PROGRAM=domprom
TARGET=bin/linux64/domprom
SOURCE= $(PROGRAM).cpp
OBJECT = $(PROGRAM).o

CC=g++
CCOPTS=-c -m64
NOTESDIR=$(Notes_ExecDirectory)
LIBS=-lnotes -lm -lpthread -lc -ldl

INCDIR = $(LOTUS)/notesapi/include
BOOTOBJS = $(LOTUS)/notesapi/lib/linux64/notes0.o $(LOTUS)/notesapi/lib/linux64/notesai0.o
DEFINES = -DGCC3 -DGCC4 -fno-strict-aliasing -DGCC_LBLB_NOT_SUPPORTED -Wformat -Wall -Wcast-align -Wconversion  -DUNIX -DLINUX -DLINUX86 -DND64 -DLINUX64 -DW -DLINUX86_64 -DDTRACE -DPTHREAD_KERNEL -D_REENTRANT -DUSE_THREADSAFE_INTERFACES -D_POSIX_THREAD_SAFE_FUNCTIONS  -DHANDLE_IS_32BITS -DHAS_IOCP -DHAS_BOOL -DHAS_DLOPEN -DUSE_PTHREAD_INTERFACES -DLARGE64_FILES -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DNDUNIX64 -DLONGIS64BIT -DPRODUCTION_VERSION -DOVERRIDEDEBUG -fPIC -Wno-write-strings

$(TARGET): $(OBJECT)
	$(CC) $(OBJECT) $(BOOTOBJS) -L$(NOTESDIR) -Wl,-rpath-link -no-pie $(NOTESDIR) $(LIBS) -o $(TARGET)

$(OBJECT): $(SOURCE)
	$(CC) $(CCOPTS) $(DEFINES) -I$(INCDIR) $(SOURCE) -o $(OBJECT)

install:
	cp -f $(TARGET) /opt/hcl/domino/notes/latest/linux/$(PROGRAM)
	chmod 755  /opt/hcl/domino/notes/latest/linux/$(PROGRAM)
clean:
	rm -f *.o
	rm -f ./$(TARGET)
