CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99 -D_XOPEN_SOURCE=700
LDFLAGS=-lcrypto

OBJS=main.o commands.o worker.o watcher.o utils.o
TARGET=backup

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
