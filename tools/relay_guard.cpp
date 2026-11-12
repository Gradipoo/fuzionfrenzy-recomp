// Stops the hosted relay after the game process is gone.
// The game exits with _Exit, so atexit in that image does not run.
#include "net/port_map.h"

int main(int argc, char** argv) { return net::GuardMain(argc, argv); }
