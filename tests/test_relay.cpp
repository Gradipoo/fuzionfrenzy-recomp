// The relay check is test_temp/loopback_regression.py.
// This file is not wired into CMake. It used to speak a client-chosen
// session id on port 9999. Do not send that header.
int main() { return 0; }
