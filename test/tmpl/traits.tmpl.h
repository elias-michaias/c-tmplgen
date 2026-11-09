// clang-format off
#ifndef TMPLGEN
typedef int T;
#endif

// @trait Eq(T)
int equal(T lhs, T rhs);
// @end

// @trait Ord(T)
int compare(T lhs, T rhs);
// @end

// @impl Eq(int)
static inline int equal(int lhs, int rhs) { return lhs == rhs; }
// @end

// @impl Ord(int)
static inline int compare(int lhs, int rhs) { return (lhs > rhs) - (lhs < rhs); }

// @impl Eq(double)
static inline int equal(double lhs, double rhs) { return lhs == rhs; }
// @end

// @impl Ord(double)
static inline int compare(double lhs, double rhs)
{
    return (lhs > rhs) - (lhs < rhs);
}
// @end

// @template T: Ord
static inline T trait_max(T lhs, T rhs)
{
    return Ord_compare(lhs, rhs) >= 0 ? lhs : rhs;
}

// @template T: Eq + Ord
static inline int trait_between_open(T value, T low, T high)
{
    return Ord_compare(value, low) > 0 && Ord_compare(value, high) < 0 &&
           !Eq_equal(value, low) && !Eq_equal(value, high);
}
