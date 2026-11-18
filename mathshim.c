// mathshim.c - the C maths functions the Kastle 2 DSP calls (expf, powf, sinf, and their kin for safety), for the Pi
// package: a plugin may not use the host's maths library. Computed in double precision; each is accurate to the last
// bit or two of a float. Only in the AArch64 package (the test builds use the C library).
// MIT, Copyright 2026 amnmkkr.
#include <stdint.h>

static double exp_d(double x) {   // e^x
  if (x > 709.0) return 1.0 / 0.0;
  if (x < -745.0) return 0.0;
  const double ln2 = 0.69314718055994530942;
  const double k = (double)(int64_t)(x / ln2 + (x >= 0 ? 0.5 : -0.5));
  const double r = x - k * ln2;   // |r| <= ln2 / 2
  double s = 1.0, t = 1.0;
  for (int i = 1; i < 18; ++i) { t *= r / i; s += t; }
  union { double d; uint64_t u; } v = {s};
  v.u += (uint64_t)((int64_t)k) << 52;
  return v.d;
}

static double log_d(double x) {   // ln x, x > 0
  if (!(x > 0.0)) return x == 0.0 ? -1.0 / 0.0 : 0.0 / 0.0;
  union { double d; uint64_t u; } v = {x};
  int e = (int)((v.u >> 52) & 0x7FF) - 1023;
  v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;   // m in [1, 2)
  double m = v.d;
  if (m > 1.41421356237309505) { m *= 0.5; ++e; }
  const double t = (m - 1.0) / (m + 1.0), t2 = t * t;
  double s = 0.0, p = t;
  for (int i = 1; i < 40; i += 2) { s += p / i; p *= t2; }
  return 2.0 * s + e * 0.69314718055994530942;
}

static double sin_d(double x) {   // sin x, any x
  const double tp = 6.283185307179586477;
  double q = x / tp;
  q -= (double)(int64_t)q;
  if (q < 0) q += 1.0;
  double r = q * tp;                           // [0, 2pi)
  if (r > 3.14159265358979324) r -= tp;        // (-pi, pi]
  if (r > 1.57079632679489662) r = 3.14159265358979324 - r;
  else if (r < -1.57079632679489662) r = -3.14159265358979324 - r;
  const double r2 = r * r;
  double s = r, t = r;
  for (int i = 1; i < 12; ++i) { t *= -r2 / ((2 * i) * (2 * i + 1)); s += t; }
  return s;
}

float expf(float x) { return (float)exp_d(x); }
float logf(float x) { return (float)log_d(x); }
float sinf(float x) { return (float)sin_d(x); }
float cosf(float x) { return (float)sin_d((double)x + 1.57079632679489662); }
float tanf(float x) { return (float)(sin_d(x) / sin_d((double)x + 1.57079632679489662)); }
float powf(float x, float y) {
  if (y == 0.0f) return 1.0f;
  if (x == 0.0f) return y > 0.0f ? 0.0f : 1.0f / 0.0f;
  if (x < 0.0f) {   // a negative base: integer powers only
    const int64_t n = (int64_t)y;
    if ((float)n != y) return 0.0f / 0.0f;
    const double m = exp_d(y * log_d(-(double)x));
    return (float)((n & 1) ? -m : m);
  }
  return (float)exp_d(y * log_d(x));
}
float exp2f(float x) { return (float)exp_d(x * 0.69314718055994530942); }
float log2f(float x) { return (float)(log_d(x) * 1.44269504088896341); }
float log10f(float x) { return (float)(log_d(x) * 0.43429448190325182); }
double pow(double x, double y) {   // (Wave Bard's quantizer, once at Init: 2^(semitones/12))
  if (y == 0.0) return 1.0;
  if (x <= 0.0) return x == 0.0 ? 0.0 : (double)powf((float)x, (float)y);
  return exp_d(y * log_d(x));
}
