#ifndef REFLECT_TYPES_H
#define REFLECT_TYPES_H
/* Types for the reflection cases (test/cases/reflect_*.c). The test project is
 * generated with --reflect=project, so every struct, union and enum declared
 * here gets a TypeId, a row in the layout tables, and a compiler check. */

#include "../lib/point.h"

typedef enum { ROLE_WARRIOR, ROLE_MAGE, ROLE_ROGUE } Role;
typedef enum { STATUS_DOWN = -1, STATUS_OK = 0, STATUS_BUFFED = 4 } Status;

typedef struct {
    char label[12];
    unsigned short weight;
} Item;

typedef struct {
    Point position;          /* nested struct from another header */
    int health;
    Role role;
    Status status;
    char name[16];
    float speed;
    _Bool alive;
} Hero;

struct Packed {              /* tag-only: reflected as struct_Packed */
    unsigned char kind;
    unsigned value : 12;     /* bitfields: bit offset and width in the table */
    unsigned flag : 1;
};

typedef union {
    int whole;
    float real;
} Number;

/* A header may use specializations of its own types once they are declared. */
#include "../gen/all.gen.h"

typedef struct {
    Vec(Item) items;         /* reflected as Vec_Item, displayed "Vec(Item)" */
    unsigned gold;
} Inventory;

#endif /* REFLECT_TYPES_H */
