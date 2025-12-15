#include "MockESP.h"

// Include the sketch file directly
// The sketch must have #ifdef LINUX_SIM guards around hardware-specific includes
#include "trelaylaatern.ino"

int main() {
    printf("--- VIRTUAL ESP32 SIMULATOR STARTED ---\n");
    setup();
    while(1) {
        loop();
        // Add a small sleep to prevent 100% CPU usage in the loop
        usleep(10000); // 10ms
    }
    return 0;
}
