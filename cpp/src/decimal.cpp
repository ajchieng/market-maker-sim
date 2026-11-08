#include "lob/decimal.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace lob {

namespace {

std::int64_t checked_mul_add(std::int64_t v, int mul, int add, std::string_view text) {
  std::int64_t out = 0;
  if (__builtin_mul_overflow(v, mul, &out) || __builtin_add_overflow(out, add, &out)) {
    throw std::invalid_argument("decimal overflows int64 ticks: " + std::string(text));
  }
  return out;
}

}  // namespace

std::int64_t to_ticks(std::string_view text, int precision) {
  while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' ||
                           text.back() == '\t')) {
    text.remove_suffix(1);
  }
  if (text.empty()) throw std::invalid_argument("empty decimal");

  // Exponent form (5e-05) is rare in Kraken's feed; fall back to a rounded double.
  if (text.find_first_of("eE") != std::string_view::npos) {
    const std::string s(text);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) throw std::invalid_argument("bad decimal: " + s);
    return std::llround(v * std::pow(10.0, precision));
  }

  bool negative = false;
  std::size_t i = 0;
  if (text[0] == '-') {
    negative = true;
    i = 1;
  }
  std::int64_t v = 0;
  int frac_digits = -1;  // -1 until we pass the '.'
  bool any_digit = false;
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '.') {
      if (frac_digits >= 0) throw std::invalid_argument("bad decimal: " + std::string(text));
      frac_digits = 0;
      continue;
    }
    if (c < '0' || c > '9') throw std::invalid_argument("bad decimal: " + std::string(text));
    any_digit = true;
    if (frac_digits >= 0) {
      if (frac_digits == precision) {
        if (c != '0') {
          throw std::invalid_argument("more decimals than precision " + std::to_string(precision) +
                                      ": " + std::string(text));
        }
        continue;
      }
      ++frac_digits;
    }
    v = checked_mul_add(v, 10, c - '0', text);
  }
  if (!any_digit) throw std::invalid_argument("bad decimal: " + std::string(text));
  for (int have = frac_digits < 0 ? 0 : frac_digits; have < precision; ++have) {
    v = checked_mul_add(v, 10, 0, text);
  }
  return negative ? -v : v;
}

std::string format_ticks(std::int64_t ticks, int precision) {
  const bool negative = ticks < 0;
  std::string digits = std::to_string(negative ? -ticks : ticks);
  if (precision > 0) {
    if (static_cast<int>(digits.size()) <= precision) {
      digits.insert(0, static_cast<std::size_t>(precision) + 1 - digits.size(), '0');
    }
    digits.insert(digits.size() - static_cast<std::size_t>(precision), 1, '.');
  }
  return negative ? "-" + digits : digits;
}

}  // namespace lob
