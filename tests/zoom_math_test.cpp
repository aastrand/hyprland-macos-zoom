#include "../src/zoom_math.hpp"

#include <cassert>
#include <cmath>

int main() {
  using MacOSZoom::boundedZoom;

  assert(std::abs(boundedZoom(1.F, 1.2F, 1.F, 40.F, 1.05F) - 1.2F) < 0.0001F);
  assert(std::abs(boundedZoom(1.2F, 1.F / 1.2F, 1.F, 40.F, 1.05F) - 1.F) <
         0.0001F);
  assert(std::abs(boundedZoom(39.F, 2.F, 1.F, 40.F, 1.05F) - 40.F) < 0.0001F);
  assert(std::abs(boundedZoom(1.02F, 0.99F, 1.F, 40.F, 1.05F) - 1.F) < 0.0001F);
  assert(std::abs(boundedZoom(NAN, 1.2F, 1.F, 40.F, 1.05F) - 1.F) < 0.0001F);
}
