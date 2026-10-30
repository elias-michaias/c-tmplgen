#ifndef TMPLGEN_PHYSICS_H
#define TMPLGEN_PHYSICS_H

#include <stddef.h>

#define NAMED_INT_ALIGNMENT(Name, Type) \
    (__alignof__(Type) >= __alignof__(int) && sizeof(#Name) > 1)
#define INT_ALIGNMENT(Type) (__alignof__(Type) >= __alignof__(int))

struct Physics {
    int x;
    int y;
};

struct Entity {
    int id;
};

struct Named {
    const char *name;
};

struct Actor {
    unsigned flags;
};

struct PhysicsBody {
    int x;
    int y;
    double mass;
};

struct NamedActor {
    int id;
    const char *name;
    unsigned flags;
};

typedef struct PhysicsBody PhysicsBody;
typedef struct NamedActor NamedActor;

#endif
