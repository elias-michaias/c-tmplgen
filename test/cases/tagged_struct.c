#include "test_types.h"
#include "../../physics.h"
#include "../../gen/all.gen.h"

int main(void)
{
    TaggedBox(TestInt) box = { 42 };
    TaggedChoice(TestInt) choice = { .value = 11 };
    Option(TestInt) first = { 2 };
    Option(TestInt) second = { 3 };
    Option(double) different = { 4.0 };
    Box(TestInt) left = { 1 };
    Box(double) right = { 2.5 };
    Box(double) selected;
    PhysicsBody body = { 5, 7, 1.25 };
    NamedActor actor = { 11, "hero", 3U };
    if (tagged_box_value(&box) != 42) return 1;
    if (tagged_choice_value(&choice) != 11) return 2;
    if (structural_pair(&body, &actor) != 16) return 3;
    if (!alignment_ok(&body)) return 4;
    if (option_pair(&first, &second) != 5) return 5;
    (void)different;
    selected = box_select(&left, &right);
    return selected.value != 2.5 || physics_sum(&body) != 12 ||
           named_actor_id(&actor) != 11;
}
