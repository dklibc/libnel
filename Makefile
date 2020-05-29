LDFLAGS+=
LIB_DIR=$(DESTDIR)/lib64
INC_DIR=$(DESTDIR)/include

.PHONY: clean all

all: ip iw libs

ip: ip.o nlcore.o nlroute.o nlog.o
	$(CC) $(LDFLAGS) -o $@ $^

iw: genlcore.o iw.o nl80211.o nlcore.o nlroute.o nlog.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -fPIC -o $@ $^

clean:
	rm ip iw *.o *.a *.so || true

libnel.so: nlcore.o nlog.o
	$(CC) -o $@ $^ -fPIC -shared

libnel-route.so: nlroute.o nlog.o libnel.so
	$(CC) -o $@ $^ -fPIC -shared -L./libnel.so

libnel-route.a: nlcore.o nlroute.o nlog.o
	ar rcs $@ $^

libnel-genl.so: nlcore.o genlcore.o nlog.o libnel.so
	$(CC) -o $@ $^ -fPIC -shared -L./libnel.so

libnel-nl80211.so: nl80211.o nlog.o libnel-genl.so
	$(CC) -o $@ $^ -fPIC -shared -L./libnel-genl.so

libs: libnel-route.so libnel-nl80211.so

libs-static: libnel-route.a

install:
	echo "" >install_manifest.txt

	mkdir -p "$(LIB_DIR)"

	cp libnel.so "$(LIB_DIR)/libnel.so"
	echo "$(LIB_DIR)/libnel.so" >>install_manifest.txt

	cp libnel-route.so "$(LIB_DIR)/libnel-route.so"
	echo "$(LIB_DIR)/libnel-route.so" >>install_manifest.txt

	cp libnel-genl.so "$(LIB_DIR)/libnel-genl.so"
	echo "$(LIB_DIR)/libnel-genl.so" >>install_manifest.txt

	cp libnel-nl80211.so "$(LIB_DIR)/libnel-nl80211.so"
	echo "$(LIB_DIR)/libnel-nl80211.so" >>install_manifest.txt

	mkdir -p "$(INC_DIR)/libnel"

	cp nlcore.h nlog.h nlroute.h genlcore.h nl80211.h "$(INC_DIR)/libnel"
	echo "$(INC_DIR)/libnel/nlcore.h" >>install_manifest.txt
	echo "$(INC_DIR)/libnel/nlog.h" >>install_manifest.txt
	echo "$(INC_DIR)/libnel/nlroute.h" >>install_manifest.txt
	echo "$(INC_DIR)/libnel/genlcore.h" >>install_manifest.txt
	echo "$(INC_DIR)/libnel/nl80211.h" >>install_manifest.txt

uninstall:
	cat install_manifest.txt | xargs rm
