#include <assert.h>
#include "../src/feedback.c"

static void advance(void) {
    assert(step_work.pending);
    fake_now += fake_delay;
    step_work.pending = false;
    step(NULL);
}

int main(void) {
    assert(init() == 0);
    assert(gpio_values[0] == 0 && gpio_values[1] == 0);
    assert(fbc_trigger(100) == -EACCES); /* boot muted */
    fbc_set_enabled(true);
    assert(fbc_trigger(0) == -EINVAL);
    assert(fbc_trigger(601) == -EINVAL);
    int invalid[] = {100, -1, 100};
    assert(fbc_trigger_pattern(invalid, 3) == -EINVAL);
    int excessive[32];
    for (int i = 0; i < 32; i++) excessive[i] = 600;
    assert(fbc_trigger_pattern(excessive, 32) == -EINVAL);
    assert(fbc_trigger_pattern(NULL, 1) == -EINVAL);
    int pattern[] = {100, 50, 150};
    assert(fbc_trigger_pattern(pattern, 3) == 0);
    pattern[0] = 500; /* caller storage can change safely */
    assert(steps[0] == 100);
    assert(fake_delay == 5 && gpio_values[0] == 0 && gpio_values[1] == 1);
    assert(fbc_trigger(100) == -EBUSY);
    step(NULL); /* stale/early callback must not begin a pulse */
    assert(gpio_values[0] == 0 && index == 0);
    advance(); assert(gpio_values[0] == 1 && fake_delay == 100);
    advance(); assert(gpio_values[0] == 0 && fake_delay == 50);
    advance(); assert(gpio_values[0] == 1 && fake_delay == 150);
    step(NULL); assert(gpio_values[0] == 1); /* don't truncate final pulse */
    advance(); assert(!fbc_is_active() && gpio_values[0] == 0 && gpio_values[1] == 0);
    assert(fbc_trigger(100) == -EBUSY); /* cooldown */
    fake_now += 100;
    assert(fbc_trigger_pattern_priority(pattern, 3, 0) == 0);
    advance();
    assert(fbc_trigger_pattern_priority(pattern, 3, 1) == 0);
    assert(gpio_values[0] == 0); /* replacement starts with motor off */
    assert(fbc_trigger_pattern_priority(pattern, 3, 1) == -EBUSY);
    fbc_set_enabled(false);
    assert(!fbc_is_active() && !step_work.pending && gpio_values[1] == 0);
    step(NULL); assert(gpio_values[0] == 0); /* stale disabled callback */
    assert(fbc_trigger(100) == -EACCES);
    return 0;
}
