// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_NUMERICS_SAFE_CONVERSIONS_IMPL_H_
#define BASE_NUMERICS_SAFE_CONVERSIONS_IMPL_H_

#include <stdint.h>

#include <limits>
#include <type_traits>

namespace base {
namespace internal {

// The std library doesn't provide a binary max_exponent for integers, however
// we can compute an analog using std::numeric_limits<>::digits.
template <typename NumericType>
struct MaxExponent {
  static const int value = std::is_floating_point<NumericType>::value
                               ? std::numeric_limits<NumericType>::max_exponent
                               : std::numeric_limits<NumericType>::digits + 1;
};

// The number of bits (including the sign) in an integer. Eliminates sizeof
// hacks.
template <typename NumericType>
struct IntegerBitsPlusSign {
  static const int value = std::numeric_limits<NumericType>::digits +
                           std::is_signed<NumericType>::value;
};

// Helper templates for integer manipulations.

template <typename Integer>
struct PositionOfSignBit {
  static const size_t value = IntegerBitsPlusSign<Integer>::value - 1;
};

template <typename T>
constexpr bool HasSignBit(T x) {
  // Cast to unsigned since right shift on signed is undefined.
  return !!(static_cast<typename std::make_unsigned<T>::type>(x) >>
            PositionOfSignBit<T>::value);
}

// This wrapper undoes the standard integer promotions.
template <typename T>
constexpr T BinaryComplement(T x) {
  return static_cast<T>(~x);
}

// Determines if a numeric value is negative without throwing compiler
// warnings on: unsigned(value) < 0.
template <typename T,
          typename std::enable_if<std::is_signed<T>::value>::type* = nullptr>
constexpr bool IsValueNegative(T value) {
  static_assert(std::is_arithmetic<T>::value, "Argument must be numeric.");
  return value < 0;
}

template <typename T,
          typename std::enable_if<!std::is_signed<T>::value>::type* = nullptr>
constexpr bool IsValueNegative(T) {
  static_assert(std::is_arithmetic<T>::value, "Argument must be numeric.");
  return false;
}

// This performs a fast negation, returning a signed value. It works on unsigned
// arguments, but probably doesn't do what you want for any unsigned value
// larger than max / 2 + 1 (i.e. signed min cast to unsigned).
template <typename T>
constexpr typename std::make_signed<T>::type ConditionalNegate(
    T x,
    bool is_negative) {
  static_assert(std::is_integral<T>::value, "Type must be integral");
  using SignedT = typename std::make_signed<T>::type;
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<SignedT>(
      (static_cast<UnsignedT>(x) ^ -SignedT(is_negative)) + is_negative);
}

// This performs a safe, non-branching absolute value via unsigned overflow.
template <typename T>
constexpr T SafeUnsignedAbsImpl(T value, T sign_mask) {
  static_assert(!std::is_signed<T>::value, "Types must be unsigned.");
  return (value ^ sign_mask) - sign_mask;
}

template <typename T,
          typename std::enable_if<std::is_integral<T>::value &&
                                  std::is_signed<T>::value>::type* = nullptr>
constexpr typename std::make_unsigned<T>::type SafeUnsignedAbs(T value) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return SafeUnsignedAbsImpl(
      static_cast<UnsignedT>(value),
      // The sign mask is all ones for negative and zero otherwise.
      static_cast<UnsignedT>(-static_cast<T>(HasSignBit(value))));
}

template <typename T,
          typename std::enable_if<std::is_integral<T>::value &&
                                  !std::is_signed<T>::value>::type* = nullptr>
constexpr T SafeUnsignedAbs(T value) {
  // T is unsigned, so |value| must already be positive.
  return static_cast<T>(value);
}

enum IntegerRepresentation {
  INTEGER_REPRESENTATION_UNSIGNED,
  INTEGER_REPRESENTATION_SIGNED
};

// A range for a given nunmeric Src type is contained for a given numeric Dst
// type if both numeric_limits<Src>::max() <= numeric_limits<Dst>::max() and
// numeric_limits<Src>::lowest() >= numeric_limits<Dst>::lowest() are true.
// We implement this as template specializations rather than simple static
// comparisons to ensure type correctness in our comparisons.
enum NumericRangeRepresentation {
  NUMERIC_RANGE_NOT_CONTAINED,
  NUMERIC_RANGE_CONTAINED
};

// Helper templates to statically determine if our destination type can contain
// maximum and minimum values represented by the source type.

template <typename Dst,
          typename Src,
          IntegerRepresentation DstSign = std::is_signed<Dst>::value
                                              ? INTEGER_REPRESENTATION_SIGNED
                                              : INTEGER_REPRESENTATION_UNSIGNED,
          IntegerRepresentation SrcSign = std::is_signed<Src>::value
                                              ? INTEGER_REPRESENTATION_SIGNED
                                              : INTEGER_REPRESENTATION_UNSIGNED>
struct StaticDstRangeRelationToSrcRange;

// Same sign: Dst is guaranteed to contain Src only if its range is equal or
// larger.
template <typename Dst, typename Src, IntegerRepresentation Sign>
struct StaticDstRangeRelationToSrcRange<Dst, Src, Sign, Sign> {
  static const NumericRangeRepresentation value =
      MaxExponent<Dst>::value >= MaxExponent<Src>::value
          ? NUMERIC_RANGE_CONTAINED
          : NUMERIC_RANGE_NOT_CONTAINED;
};

// Unsigned to signed: Dst is guaranteed to contain source only if its range is
// larger.
template <typename Dst, typename Src>
struct StaticDstRangeRelationToSrcRange<Dst,
                                        Src,
                                        INTEGER_REPRESENTATION_SIGNED,
                                        INTEGER_REPRESENTATION_UNSIGNED> {
  static const NumericRangeRepresentation value =
      MaxExponent<Dst>::value > MaxExponent<Src>::value
          ? NUMERIC_RANGE_CONTAINED
          : NUMERIC_RANGE_NOT_CONTAINED;
};

// Signed to unsigned: Dst cannot be statically determined to contain Src.
template <typename Dst, typename Src>
struct StaticDstRangeRelationToSrcRange<Dst,
                                        Src,
                                        INTEGER_REPRESENTATION_UNSIGNED,
                                        INTEGER_REPRESENTATION_SIGNED> {
  static const NumericRangeRepresentation value = NUMERIC_RANGE_NOT_CONTAINED;
};

enum RangeConstraintEnum {
  RANGE_VALID = 0x0,      // Value can be represented by the destination type.
  RANGE_OVERFLOW = 0x1,   // Value would overflow.
  RANGE_UNDERFLOW = 0x2,  // Value would underflow.
  RANGE_INVALID = RANGE_UNDERFLOW | RANGE_OVERFLOW  // Invalid (i.e. NaN).
};

// This class wraps the range constraints as separate booleans so the compiler
// can identify constants and eliminate unused code paths.
class RangeConstraint {
 public:
  constexpr RangeConstraint(bool is_in_upper_bound, bool is_in_lower_bound)
      : is_overflow_(!is_in_upper_bound), is_underflow_(!is_in_lower_bound) {}
  constexpr RangeConstraint() : is_overflow_(0), is_underflow_(0) {}
  constexpr bool IsValid() const { return !is_overflow_ && !is_underflow_; }
  constexpr bool IsInvalid() const { return is_overflow_ && is_underflow_; }
  constexpr bool IsOverflow() const { return is_overflow_ && !is_underflow_; }
  constexpr bool IsUnderflow() const { return !is_overflow_ && is_underflow_; }

  // These are some wrappers to make the tests a bit cleaner.
  constexpr operator RangeConstraintEnum() const {
    return static_cast<RangeConstraintEnum>(
        static_cast<int>(is_overflow_) | static_cast<int>(is_underflow_) << 1);
  }
  constexpr bool operator==(const RangeConstraintEnum rhs) const {
    return rhs == static_cast<RangeConstraintEnum>(*this);
  }

 private:
  const bool is_overflow_;
  const bool is_underflow_;
};

// The following helper template addresses a corner case in range checks for
// conversion from a floating-point type to an integral type of smaller range
// but larger precision (e.g. float -> unsigned). The problem is as follows:
//   1. Integral maximum is always one less than a power of two, so it must be
//      truncated to fit the mantissa of the floating point. The direction of
//      rounding is implementation defined, but by default it's always IEEE
//      floats, which round to nearest and thus result in a value of larger
//      magnitude than the integral value.
//      Example: float f = UINT_MAX; // f is 4294967296f but UINT_MAX
//                                   // is 4294967295u.
//   2. If the floating point value is equal to the promoted integral maximum
//      value, a range check will erroneously pass.
//      Example: (4294967296f <= 4294967295u) // This is true due to a precision
//                                            // loss in rounding up to float.
//   3. When the floating point value is then converted to an integral, the
//      resulting value is out of range for the target integral type and
//      thus is implementation defined.
//      Example: unsigned u = (float)INT_MAX; // u will typically overflow to 0.
// To fix this bug we manually truncate the maximum value when the destination
// type is an integral of larger precision than the source floating-point type,
// such that the resulting maximum is represented exactly as a floating point.
template <typename Dst, typename Src, template <typename> class Bounds>
struct NarrowingRange {
  using SrcLimits = std::numeric_limits<Src>;
  using DstLimits = typename std::numeric_limits<Dst>;

  // Computes the mask required to make an accurate comparison between types.
  static const int kShift =
      (MaxExponent<Src>::value > MaxExponent<Dst>::value &&
       SrcLimits::digits < DstLimits::digits)
          ? (DstLimits::digits - SrcLimits::digits)
          : 0;
  template <
      typename T,
      typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>

  // Masks out the integer bits that are beyond the precision of the
  // intermediate type used for comparison.
  static constexpr T Adjust(T value) {
    static_assert(std::is_same<T, Dst>::value, "");
    static_assert(kShift < DstLimits::digits, "");
    return static_cast<T>(
        ConditionalNegate(SafeUnsignedAbs(value) & ~((T(1) << kShift) - T(1)),
                          IsValueNegative(value)));
  }

  template <typename T,
            typename std::enable_if<std::is_floating_point<T>::value>::type* =
                nullptr>
  static constexpr T Adjust(T value) {
    static_assert(std::is_same<T, Dst>::value, "");
    static_assert(kShift == 0, "");
    return value;
  }

  static constexpr Dst max() { return Adjust(Bounds<Dst>::max()); }
  static constexpr Dst lowest() { return Adjust(Bounds<Dst>::lowest()); }
};

template <typename Dst,
          typename Src,
          template <typename> class Bounds,
          IntegerRepresentation DstSign = std::is_signed<Dst>::value
                                              ? INTEGER_REPRESENTATION_SIGNED
                                              : INTEGER_REPRESENTATION_UNSIGNED,
          IntegerRepresentation SrcSign = std::is_signed<Src>::value
                                              ? INTEGER_REPRESENTATION_SIGNED
                                              : INTEGER_REPRESENTATION_UNSIGNED,
          NumericRangeRepresentation DstRange =
              StaticDstRangeRelationToSrcRange<Dst, Src>::value>
struct DstRangeRelationToSrcRangeImpl;

// The following templates are for ranges that must be verified at runtime. We
// split it into checks based on signedness to avoid confusing casts and
// compiler warnings on signed an unsigned comparisons.

// Same sign narrowing: The range is contained for normal limits.
template <typename Dst,
          typename Src,
          template <typename> class Bounds,
          IntegerRepresentation DstSign,
          IntegerRepresentation SrcSign>
struct DstRangeRelationToSrcRangeImpl<Dst,
                                      Src,
                                      Bounds,
                                      DstSign,
                                      SrcSign,
                                      NUMERIC_RANGE_CONTAINED> {
  static constexpr RangeConstraint Check(Src value) {
    using SrcLimits = std::numeric_limits<Src>;
    using DstLimits = NarrowingRange<Dst, Src, Bounds>;
    return RangeConstraint(
        static_cast<Dst>(SrcLimits::max()) <= DstLimits::max() ||
            static_cast<Dst>(value) <= DstLimits::max(),
        static_cast<Dst>(SrcLimits::lowest()) >= DstLimits::lowest() ||
            static_cast<Dst>(value) >= DstLimits::lowest());
  }
};

// Signed to signed narrowing: Both the upper and lower boundaries may be
// exceeded for standard limits.
template <typename Dst, typename Src, template <typename> class Bounds>
struct DstRangeRelationToSrcRangeImpl<Dst,
                                      Src,
                                      Bounds,
                                      INTEGER_REPRESENTATION_SIGNED,
                                      INTEGER_REPRESENTATION_SIGNED,
                                      NUMERIC_RANGE_NOT_CONTAINED> {
  static constexpr RangeConstraint Check(Src value) {
    using DstLimits = NarrowingRange<Dst, Src, Bounds>;
    return RangeConstraint(value <= DstLimits::max(),
                           value >= DstLimits::lowest());
  }
};

// Unsigned to unsigned narrowing: Only the upper bound can be exceeded for
// standard limits.
template <typename Dst, typename Src, template <typename> class Bounds>
struct DstRangeRelationToSrcRangeImpl<Dst,
                                      Src,
                                      Bounds,
                                      INTEGER_REPRESENTATION_UNSIGNED,
                                      INTEGER_REPRESENTATION_UNSIGNED,
                                      NUMERIC_RANGE_NOT_CONTAINED> {
  static constexpr RangeConstraint Check(Src value) {
    using DstLimits = NarrowingRange<Dst, Src, Bounds>;
    return RangeConstraint(
        value <= DstLimits::max(),
        DstLimits::lowest() == Dst(0) || value >= DstLimits::lowest());
  }
};

// Unsigned to signed: Only the upper bound can be exceeded for standard limits.
template <typename Dst, typename Src, template <typename> class Bounds>
struct DstRangeRelationToSrcRangeImpl<Dst,
                                      Src,
                                      Bounds,
                                      INTEGER_REPRESENTATION_SIGNED,
                                      INTEGER_REPRESENTATION_UNSIGNED,
                                      NUMERIC_RANGE_NOT_CONTAINED> {
  static constexpr RangeConstraint Check(Src value) {
    using DstLimits = NarrowingRange<Dst, Src, Bounds>;
    using Promotion = decltype(Src() + Dst());
    return RangeConstraint(static_cast<Promotion>(value) <=
                               static_cast<Promotion>(DstLimits::max()),
                           DstLimits::lowest() <= Dst(0) ||
                               static_cast<Promotion>(value) >=
                                   static_cast<Promotion>(DstLimits::lowest()));
  }
};

// Signed to unsigned: The upper boundary may be exceeded for a narrower Dst,
// and any negative value exceeds the lower boundary for standard limits.
template <typename Dst, typename Src, template <typename> class Bounds>
struct DstRangeRelationToSrcRangeImpl<Dst,
                                      Src,
                                      Bounds,
                                      INTEGER_REPRESENTATION_UNSIGNED,
                                      INTEGER_REPRESENTATION_SIGNED,
                                      NUMERIC_RANGE_NOT_CONTAINED> {
  static constexpr RangeConstraint Check(Src value) {
    using SrcLimits = std::numeric_limits<Src>;
    using DstLimits = NarrowingRange<Dst, Src, Bounds>;
    using Promotion = decltype(Src() + Dst());
    return RangeConstraint(
        static_cast<Promotion>(SrcLimits::max()) <=
                static_cast<Promotion>(DstLimits::max()) ||
            static_cast<Promotion>(value) <=
                static_cast<Promotion>(DstLimits::max()),
        value >= Src(0) && (DstLimits::lowest() == 0 ||
                            static_cast<Dst>(value) >= DstLimits::lowest()));
  }
};

template <typename Dst,
          template <typename> class Bounds = std::numeric_limits,
          typename Src>
constexpr RangeConstraint DstRangeRelationToSrcRange(Src value) {
  static_assert(std::is_arithmetic<Src>::value, "Argument must be numeric.");
  static_assert(std::is_arithmetic<Dst>::value, "Result must be numeric.");
  static_assert(Bounds<Dst>::lowest() < Bounds<Dst>::max(), "");
  return DstRangeRelationToSrcRangeImpl<Dst, Src, Bounds>::Check(value);
}

// Integer promotion templates used by the portable checked integer arithmetic.
template <size_t Size, bool IsSigned>
struct IntegerForDigitsAndSign;

#define INTEGER_FOR_DIGITS_AND_SIGN(I)                          \
  template <>                                                   \
  struct IntegerForDigitsAndSign<IntegerBitsPlusSign<I>::value, \
                                 std::is_signed<I>::value> {    \
    using type = I;                                             \
  }

INTEGER_FOR_DIGITS_AND_SIGN(int8_t);
INTEGER_FOR_DIGITS_AND_SIGN(uint8_t);
INTEGER_FOR_DIGITS_AND_SIGN(int16_t);
INTEGER_FOR_DIGITS_AND_SIGN(uint16_t);
INTEGER_FOR_DIGITS_AND_SIGN(int32_t);
INTEGER_FOR_DIGITS_AND_SIGN(uint32_t);
INTEGER_FOR_DIGITS_AND_SIGN(int64_t);
INTEGER_FOR_DIGITS_AND_SIGN(uint64_t);
#undef INTEGER_FOR_DIGITS_AND_SIGN

// WARNING: We have no IntegerForSizeAndSign<16, *>. If we ever add one to
// support 128-bit math, then the ArithmeticPromotion template below will need
// to be updated (or more likely replaced with a decltype expression).
static_assert(IntegerBitsPlusSign<intmax_t>::value == 64,
              "Max integer size not supported for this toolchain.");

template <typename Integer, bool IsSigned = std::is_signed<Integer>::value>
struct TwiceWiderInteger {
  using type =
      typename IntegerForDigitsAndSign<IntegerBitsPlusSign<Integer>::value * 2,
                                       IsSigned>::type;
};

enum ArithmeticPromotionCategory {
  LEFT_PROMOTION,  // Use the type of the left-hand argument.
  RIGHT_PROMOTION  // Use the type of the right-hand argument.
};

// Determines the type that can represent the largest positive value.
template <typename Lhs,
          typename Rhs,
          ArithmeticPromotionCategory Promotion =
              (MaxExponent<Lhs>::value > MaxExponent<Rhs>::value)
                  ? LEFT_PROMOTION
                  : RIGHT_PROMOTION>
struct MaxExponentPromotion;

template <typename Lhs, typename Rhs>
struct MaxExponentPromotion<Lhs, Rhs, LEFT_PROMOTION> {
  using type = Lhs;
};

template <typename Lhs, typename Rhs>
struct MaxExponentPromotion<Lhs, Rhs, RIGHT_PROMOTION> {
  using type = Rhs;
};

// Determines the type that can represent the lowest arithmetic value.
template <typename Lhs,
          typename Rhs,
          ArithmeticPromotionCategory Promotion =
              std::is_signed<Lhs>::value
                  ? (std::is_signed<Rhs>::value
                         ? (MaxExponent<Lhs>::value > MaxExponent<Rhs>::value
                                ? LEFT_PROMOTION
                                : RIGHT_PROMOTION)
                         : LEFT_PROMOTION)
                  : (std::is_signed<Rhs>::value
                         ? RIGHT_PROMOTION
                         : (MaxExponent<Lhs>::value < MaxExponent<Rhs>::value
                                ? LEFT_PROMOTION
                                : RIGHT_PROMOTION))>
struct LowestValuePromotion;

template <typename Lhs, typename Rhs>
struct LowestValuePromotion<Lhs, Rhs, LEFT_PROMOTION> {
  using type = Lhs;
};

template <typename Lhs, typename Rhs>
struct LowestValuePromotion<Lhs, Rhs, RIGHT_PROMOTION> {
  using type = Rhs;
};

// Determines the type that is best able to represent an arithmetic result.
template <
    typename Lhs,
    typename Rhs = Lhs,
    bool is_intmax_type =
        std::is_integral<typename MaxExponentPromotion<Lhs, Rhs>::type>::value&&
            IntegerBitsPlusSign<typename MaxExponentPromotion<Lhs, Rhs>::type>::
                value == IntegerBitsPlusSign<intmax_t>::value,
    bool is_max_exponent =
        StaticDstRangeRelationToSrcRange<
            typename MaxExponentPromotion<Lhs, Rhs>::type,
            Lhs>::value ==
        NUMERIC_RANGE_CONTAINED&& StaticDstRangeRelationToSrcRange<
            typename MaxExponentPromotion<Lhs, Rhs>::type,
            Rhs>::value == NUMERIC_RANGE_CONTAINED>
struct BigEnoughPromotion;

// The side with the max exponent is big enough.
template <typename Lhs, typename Rhs, bool is_intmax_type>
struct BigEnoughPromotion<Lhs, Rhs, is_intmax_type, true> {
  using type = typename MaxExponentPromotion<Lhs, Rhs>::type;
  static const bool is_contained = true;
};

// We can use a twice wider type to fit.
template <typename Lhs, typename Rhs>
struct BigEnoughPromotion<Lhs, Rhs, false, false> {
  using type =
      typename TwiceWiderInteger<typename MaxExponentPromotion<Lhs, Rhs>::type,
                                 std::is_signed<Lhs>::value ||
                                     std::is_signed<Rhs>::value>::type;
  static const bool is_contained = true;
};

// No type is large enough.
template <typename Lhs, typename Rhs>
struct BigEnoughPromotion<Lhs, Rhs, true, false> {
  using type = typename MaxExponentPromotion<Lhs, Rhs>::type;
  static const bool is_contained = false;
};

// We can statically check if operations on the provided types can wrap, so we
// can skip the checked operations if they're not needed. So, for an integer we
// care if the destination type preserves the sign and is twice the width of
// the source.
template <typename T, typename Lhs, typename Rhs>
struct IsIntegerArithmeticSafe {
  static const bool value =
      !std::is_floating_point<T>::value &&
      StaticDstRangeRelationToSrcRange<T, Lhs>::value ==
          NUMERIC_RANGE_CONTAINED &&
      IntegerBitsPlusSign<T>::value >= (2 * IntegerBitsPlusSign<Lhs>::value) &&
      StaticDstRangeRelationToSrcRange<T, Rhs>::value !=
          NUMERIC_RANGE_CONTAINED &&
      IntegerBitsPlusSign<T>::value >= (2 * IntegerBitsPlusSign<Rhs>::value);
};

// This hacks around libstdc++ 4.6 missing stuff in type_traits.
#if defined(__GLIBCXX__)
#define PRIV_GLIBCXX_4_7_0 20120322
#define PRIV_GLIBCXX_4_5_4 20120702
#define PRIV_GLIBCXX_4_6_4 20121127
#if (__GLIBCXX__ < PRIV_GLIBCXX_4_7_0 || __GLIBCXX__ == PRIV_GLIBCXX_4_5_4 || \
     __GLIBCXX__ == PRIV_GLIBCXX_4_6_4)
#define PRIV_USE_FALLBACKS_FOR_OLD_GLIBCXX
#undef PRIV_GLIBCXX_4_7_0
#undef PRIV_GLIBCXX_4_5_4
#undef PRIV_GLIBCXX_4_6_4
#endif
#endif

// Extracts the underlying type from an enum.
template <typename T, bool is_enum = std::is_enum<T>::value>
struct ArithmeticOrUnderlyingEnum;

template <typename T>
struct ArithmeticOrUnderlyingEnum<T, true> {
#if defined(PRIV_USE_FALLBACKS_FOR_OLD_GLIBCXX)
  using type = __underlying_type(T);
#else
  using type = typename std::underlying_type<T>::type;
#endif
  static const bool value = std::is_arithmetic<type>::value;
};

#if defined(PRIV_USE_FALLBACKS_FOR_OLD_GLIBCXX)
#undef PRIV_USE_FALLBACKS_FOR_OLD_GLIBCXX
#endif

template <typename T>
struct ArithmeticOrUnderlyingEnum<T, false> {
  using type = T;
  static const bool value = std::is_arithmetic<type>::value;
};

// The following are helper templates used in the CheckedNumeric class.
template <typename T>
class CheckedNumeric;

template <typename T>
class StrictNumeric;

// Used to treat CheckedNumeric and arithmetic underlying types the same.
template <typename T>
struct UnderlyingType {
  using type = typename ArithmeticOrUnderlyingEnum<T>::type;
  static const bool is_numeric = std::is_arithmetic<type>::value;
  static const bool is_checked = false;
  static const bool is_strict = false;
};

template <typename T>
struct UnderlyingType<CheckedNumeric<T>> {
  using type = T;
  static const bool is_numeric = true;
  static const bool is_checked = true;
  static const bool is_strict = false;
};

template <typename T>
struct UnderlyingType<StrictNumeric<T>> {
  using type = T;
  static const bool is_numeric = true;
  static const bool is_checked = false;
  static const bool is_strict = true;
};

template <typename L, typename R>
struct IsCheckedOp {
  static const bool value =
      UnderlyingType<L>::is_numeric && UnderlyingType<R>::is_numeric &&
      (UnderlyingType<L>::is_checked || UnderlyingType<R>::is_checked);
};

template <typename L, typename R>
struct IsStrictOp {
  static const bool value =
      UnderlyingType<L>::is_numeric && UnderlyingType<R>::is_numeric &&
      (UnderlyingType<L>::is_strict || UnderlyingType<R>::is_strict);
};

template <typename L, typename R>
constexpr bool IsLessImpl(const L lhs,
                          const R rhs,
                          const RangeConstraint l_range,
                          const RangeConstraint r_range) {
  return l_range == RANGE_UNDERFLOW || r_range == RANGE_OVERFLOW ||
         (l_range == r_range &&
          static_cast<decltype(lhs + rhs)>(lhs) <
              static_cast<decltype(lhs + rhs)>(rhs));
}

template <typename L, typename R>
struct IsLess {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  static constexpr bool Test(const L lhs, const R rhs) {
    return IsLessImpl(lhs, rhs, DstRangeRelationToSrcRange<R>(lhs),
                      DstRangeRelationToSrcRange<L>(rhs));
  }
};

template <typename L, typename R>
constexpr bool IsLessOrEqualImpl(const L lhs,
                                 const R rhs,
                                 const RangeConstraint l_range,
                                 const RangeConstraint r_range) {
  return l_range == RANGE_UNDERFLOW || r_range == RANGE_OVERFLOW ||
         (l_range == r_range &&
          static_cast<decltype(lhs + rhs)>(lhs) <=
              static_cast<decltype(lhs + rhs)>(rhs));
}

template <typename L, typename R>
struct IsLessOrEqual {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  static constexpr bool Test(const L lhs, const R rhs) {
    return IsLessOrEqualImpl(lhs, rhs, DstRangeRelationToSrcRange<R>(lhs),
                             DstRangeRelationToSrcRange<L>(rhs));
  }
};

template <typename L, typename R>
constexpr bool IsGreaterImpl(const L lhs,
                             const R rhs,
                             const RangeConstraint l_range,
                             const RangeConstraint r_range) {
  return l_range == RANGE_OVERFLOW || r_range == RANGE_UNDERFLOW ||
         (l_range == r_range &&
          static_cast<decltype(lhs + rhs)>(lhs) >
              static_cast<decltype(lhs + rhs)>(rhs));
}

template <typename L, typename R>
struct IsGreater {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  static constexpr bool Test(const L lhs, const R rhs) {
    return IsGreaterImpl(lhs, rhs, DstRangeRelationToSrcRange<R>(lhs),
                         DstRangeRelationToSrcRange<L>(rhs));
  }
};

template <typename L, typename R>
constexpr bool IsGreaterOrEqualImpl(const L lhs,
                                    const R rhs,
                                    const RangeConstraint l_range,
                                    const RangeConstraint r_range) {
  return l_range == RANGE_OVERFLOW || r_range == RANGE_UNDERFLOW ||
         (l_range == r_range &&
          static_cast<decltype(lhs + rhs)>(lhs) >=
              static_cast<decltype(lhs + rhs)>(rhs));
}

template <typename L, typename R>
struct IsGreaterOrEqual {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  static constexpr bool Test(const L lhs, const R rhs) {
    return IsGreaterOrEqualImpl(lhs, rhs, DstRangeRelationToSrcRange<R>(lhs),
                                DstRangeRelationToSrcRange<L>(rhs));
  }
};

template <typename L, typename R>
struct IsEqual {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  static constexpr bool Test(const L lhs, const R rhs) {
    return DstRangeRelationToSrcRange<R>(lhs) ==
               DstRangeRelationToSrcRange<L>(rhs) &&
           static_cast<decltype(lhs + rhs)>(lhs) ==
               static_cast<decltype(lhs + rhs)>(rhs);
  }
};

template <typename L, typename R>
struct IsNotEqual {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  static constexpr bool Test(const L lhs, const R rhs) {
    return DstRangeRelationToSrcRange<R>(lhs) !=
               DstRangeRelationToSrcRange<L>(rhs) ||
           static_cast<decltype(lhs + rhs)>(lhs) !=
               static_cast<decltype(lhs + rhs)>(rhs);
  }
};

// These perform the actual math operations on the CheckedNumerics.
// Binary arithmetic operations.
template <template <typename, typename> class C, typename L, typename R>
constexpr bool SafeCompare(const L lhs, const R rhs) {
  static_assert(std::is_arithmetic<L>::value && std::is_arithmetic<R>::value,
                "Types must be numeric.");
  using Promotion = BigEnoughPromotion<L, R>;
  using BigType = typename Promotion::type;
  return Promotion::is_contained
             // Force to a larger type for speed if both are contained.
             ? C<BigType, BigType>::Test(
                   static_cast<BigType>(static_cast<L>(lhs)),
                   static_cast<BigType>(static_cast<R>(rhs)))
             // Let the template functions figure it out for mixed types.
             : C<L, R>::Test(lhs, rhs);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_NUMERICS_SAFE_CONVERSIONS_IMPL_H_
