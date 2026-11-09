// clang-format off
#ifndef TMPLGEN
typedef int T;
#define Crate(...) Crate
#define Duo(first, second) Duo
#endif

// @template T
typedef struct { T value; } Crate;

// @trait Label(T)
int label(T value);
// @end

// @impl Label(int)
static inline int label(int value) { return value; }
// @end

// @impl Label(double)
static inline int label(double value) { return (int)value; }
// @end

// A blanket impl over Crate(T) that relies on the bound on T.
// @template T: Label
// @impl Label(Crate(T))
static inline int label(Crate(T) value) { return 10 + Label_label(value.value); }
// @end

// Declarations after a free-variable impl use their own annotations. This
// guards against an impl's consumed @template line shifting later ones.
// @template T, U
typedef struct {
    T first;
    U second;
} Duo;

// @trait Add(T)
T add(T lhs, T rhs);
// @end

// @impl Add(int)
static inline int add(int lhs, int rhs) { return lhs + rhs; }
// @end

// @impl Add(double)
static inline double add(double lhs, double rhs) { return lhs + rhs; }
// @end

// @template T, U
static inline Duo(T, U) duo_make(T first, U second)
{
    Duo(T, U) duo = { first, second };
    return duo;
}

// Each half must dispatch to its own type, not the first impl variable.
// @template T: Add, U: Add
// @impl Add(Duo(T, U))
static inline Duo(T, U) add(Duo(T, U) lhs, Duo(T, U) rhs)
{
    Duo(T, U) result = {
        Add_add(lhs.first, rhs.first),
        Add_add(lhs.second, rhs.second)
    };
    return result;
}
// @end

// A concrete Crate(int) impl must win over the blanket one.
// @impl Label(Crate(int))
static inline int label(Crate(int) value) { return 100 + value.value; }
// @end
