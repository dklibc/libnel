LDFLAGS+=
LIB_DIR="$(DESTDIR)/lib64"
INC_DIR="$(DESTDIR)/include"

.PHONY: clean all

all: ip libs

ip: ip.o nlcore.o nlroute.o nlog.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -fPIC -o $@ $^

clean:
	rm -f ip *.o *.a *.so 2>/dev/null

libnel.so: nlcore.o nlog.o
	$(CC) -o $@ $^ -fPIC -shared

libnel-route.so: nlroute.o nlog.o libnel.so
	$(CC) -o $@ $^ -fPIC -shared -L./libnel.so

libnel-route.a: nlcore.o nlroute.o nlog.o
	ar rcs $@ $^

libs: libnel-route.so

libs-static: libnel-route.a

install:
	echo "" >install_manifest.txt

	mkdir -p "$(LIB_DIR)"

	cp libnel.so "$(LIB_DIR)/libnel.so"
	echo "$(LIB_DIR)/libnel.so" >>install_manifest.txt

	cp libnel-route.so "$(LIB_DIR)/libnel-route.so"
	echo "$(LIB_DIR)/libnel-route.so" >>install_manifest.txt

	mkdir -p "$(INC_DIR)/libnel"

	cp nlcore.h nlog.h nlroute.h "$(INC_DIR)/libnel"
	echo "$(INC_DIR)/libnel/nlcore.h" >>install_manifest.txt
	echo "$(INC_DIR)/libnel/nlog.h" >>install_manifest.txt
	echo "$(INC_DIR)/libnel/nlroute.h" >>install_manifest.txt

uninstall:
	cat install_manifest.txt | xargs rm
