BUNDLE = syncrose.lv2
INSTALL_DIR = /usr/local/lib/lv2
CC=gcc

$(BUNDLE): manifest.ttl syncrose.ttl syncrose.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	cp manifest.ttl syncrose.ttl syncrose.so $(BUNDLE)

syncrose.so: syncrose.c
	$(CC) -shared -Wall -fPIC -DPIC syncrose.c `pkg-config --cflags --libs lv2 sndfile samplerate` -lexpat -lm -o syncrose.so

install: $(BUNDLE)
	mkdir -p $(INSTALL_DIR)
	rm -rf $(INSTALL_DIR)/$(BUNDLE)
	cp -R $(BUNDLE) $(INSTALL_DIR)

clean:
	rm -rf $(BUNDLE) syncrose.so
