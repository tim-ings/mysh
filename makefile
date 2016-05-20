# mysh makefile
# author = Tim Ings, 21716194

# vars
TARGET = mysh
LIBS = -lm
CC = gcc
CFLAGS = -g -Wall -std=c99

# keywords
.PHONY: default all clean

# main
default: $(TARGET)
all: default

# get all files with .c, .h extensions
OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# preserve these files
.PRECIOUS: $(TARGET) $(OBJECTS)

# compile binary
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

# clean up
clean:
	-rm -f *.o
	-rm -f $(TARGET)
