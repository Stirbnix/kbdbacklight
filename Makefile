TARGET = kbdbacklight

#DEFS = -D_ODROID_
LIBS = -lpthread -lrt
LFLAGS = -s

all:
	$(CC) -o $(TARGET) $(TARGET).c $(LIBS) $(LFLAGS) $(DEFS)

install:
	sudo cp $(TARGET) /usr/bin/
	sudo cp $(TARGET).service /etc/systemd/system/ && \
	sudo systemctl daemon-reload && \
	sudo systemctl enable $(TARGET) && \
	sudo systemctl start $(TARGET)

.PHONY: clean

clean:
	rm -f *.o *~
