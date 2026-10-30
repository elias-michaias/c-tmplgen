#include "test_types.h"
#include "../lib/physics.h"
#include "../gen/all.gen.h"

int main(void)
{
    TaggedBox(TestInt) box = { 42 };
    TaggedChoice(TestInt) choice = { .value = 11 };
    Option(TestInt) first = option_some(2);
    Option(TestInt) second = option_some(3);
    Option(double) different = option_some(4.0);
    Option(TestInt) empty = { 0 };
    Box(TestInt) left = { 1 };
    Box(double) right = { 2.5 };
    Box(double) selected;
    PhysicsBody body = { 5, 7, 1.25 };
    NamedActor actor = { 11, "hero", 3U };
    int is_empty = 0;
    TestInt scalar = 9;
    TestInt scalar2 = 10;
    if (tagged_box_value(&box) != 42) return 1;
    if (tagged_choice_value(&choice) != 11) return 2;
    if (structural_pair(&body, &actor) != 16) return 3;
    if (!alignment_ok(&body)) return 4;
    if (!same_size(scalar, scalar2)) return 5;
    if (option_pair(&first, &second) != 5) return 6;
    option_none(&empty);
    switch (empty.tag) {
    case OPTION_NONE:
        is_empty = !option_is_some(&empty);
        break;
    case OPTION_SOME:
        is_empty = 0;
        break;
    }
    if (!is_empty) return 7;
    (void)different;
    selected = box_select(&left, &right);
    return selected.value != 2.5 || physics_sum(&body) != 12 ||
           named_actor_id(&actor) != 11;
}
