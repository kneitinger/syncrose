BUNDLE = syncrose.lv2
INSTALL_DIR = /usr/local/lib/lv2
CC=gcc

$(BUNDLE): manifest.ttl syncrose.ttl syncrose.so syncrose_ui.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	cp clip.wav manifest.ttl syncrose.ttl syncrose.so syncrose_ui.so $(BUNDLE)

syncrose.so: syncrose.c
	$(CC) -shared -Wall -fPIC -DPIC syncrose.c `pkg-config --cflags --libs lv2 sndfile samplerate` -lexpat -lm -o syncrose.so

syncrose_ui.so: syncrose_ui.c
	$(CC) -shared -Wall -fPIC -DPIC syncrose_ui.c `pkg-config --cflags --libs lv2 gtk+-2.0 sndfile samplerate` -lexpat -lm -o syncrose_ui.so

install: $(BUNDLE)

	mkdir -p $(INSTALL_DIR)
	rm -rf $(INSTALL_DIR)/$(BUNDLE)
	cp -R $(BUNDLE) $(INSTALL_DIR)

clean:
	rm -rf $(BUNDLE) syncrose.so
