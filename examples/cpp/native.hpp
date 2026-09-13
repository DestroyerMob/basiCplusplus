#pragma once
#include <stdint.h>

namespace demo {
// The overload's exact parameter and return types select the generated bridge.
int64_t clamp(int64_t value, int64_t low, int64_t high);
double clamp(double value, double low, double high);
double positive_root(double value);
void store(int64_t* destination, int64_t value);
}
