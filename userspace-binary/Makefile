CC = gcc
CFLAGS = -Wall -Wextra -Ofast -std=c99
TARGET = disk_led_monitor
SOURCE = disk_led_monitor.c
SERVICE_FILE = disk-led-monitor.service

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

install-service: install
	sudo cp $(SERVICE_FILE) /etc/systemd/system/
	sudo systemctl daemon-reload
	sudo systemctl enable $(SERVICE_FILE)
	@echo "Service installed and enabled. Start with: sudo systemctl start $(SERVICE_FILE)"

uninstall:
	sudo systemctl stop $(SERVICE_FILE) 2>/dev/null || true
	sudo systemctl disable $(SERVICE_FILE) 2>/dev/null || true
	sudo rm -f /etc/systemd/system/$(SERVICE_FILE)
	sudo systemctl daemon-reload
	sudo rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install install-service uninstall 
