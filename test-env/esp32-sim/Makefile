all: esp32-sim

esp32-sim: main.cpp MockESP.h trelaylaatern.ino
	g++ -D LINUX_SIM -I. -O2 -pthread -o esp32-sim main.cpp -lcurl

clean:
	rm -f esp32-sim
