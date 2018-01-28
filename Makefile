CC = gcc
CFLAGS  = -Wall -fPIC -s -O3 -march=armv6 -mfloat-abi=hard -mfpu=vfp -I./include
LDFLAGS = -L./lib -Wl,-rpath,/usr/local/lib -lcurl -lwiringPi
#LDFLAGS = -L./lib -Wl,-rpath,/usr/local/lib /usr/local/lib/libcurl.a /usr/local/lib/libnettle.a /usr/local/lib/libssh2.a /usr/local/lib/libgcrypt.a /usr/local/lib/libgnutls.a /usr/local/lib/libidn.a /usr/local/lib/libgpg-error.a /usr/local/lib/libtasn1.a -lwiringPi 
EXECUTABLE = sbpd

SOURCES = control.c discovery.c GPIO.c sbpd.c servercomm.c
DEPS = control.h discovery.h GPIO.h sbpd.h servercomm.h

OBJECTS = $(SOURCES:.c=.o)

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJECTS): $(DEPS)

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
