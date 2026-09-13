#include "native.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demo {
int64_t clamp(int64_t value, int64_t low, int64_t high) {
    return std::clamp(value, low, high);
}

double clamp(double value, double low, double high) {
    return std::clamp(value, low, high);
}

double positive_root(double value) {
    if (value < 0) throw std::domain_error("positive_root requires a nonnegative value");
    return std::sqrt(value);
}

void store(int64_t* destination, int64_t value) {
    // Borrowed only for this call; the foreign function must not retain it.
    *destination = value;
}
}
