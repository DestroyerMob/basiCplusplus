#include <stdint.h>

// BasicC's int boundary maps to int64_t, not C's usually narrower int.
int64_t native_answer(void) {
    return 42;
}

double native_half(double value) {
    return value / 2.0;
}

// This borrowed pointer is used only during the call; C must not retain it.
void native_set_answer(int64_t* value) {
    *value = 42;
}
