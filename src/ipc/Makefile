CC = gcc
CFLAGS = -O2 -static -Wall -Wextra
LDFLAGS = -lws2_32
TARGET = wenativehost.exe
SOURCES = wenativehost.c ipc_abstraction.c simple_json.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean


#all:
#	gcc -O2 -static -o wenativehost.exe wenativehost.c ipc_abstraction.c -lws2_32
#	gcc -O2 -o wenativehost wenativehost.c ipc_abstraction.c -lpthread
#	cl /O2 /MT wenativehost.c ipc_abstraction.c /Fe:wenativehost.exe
