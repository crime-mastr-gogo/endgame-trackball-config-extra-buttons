#include <assert.h>
#include <ankur/policy.h>
int main(void) {
    struct ankur_settings good = {1, 6, 4, 1, 1, 1};
    assert(sizeof(good) == 6);
    assert(ankur_settings_valid(&good));
    for (unsigned field = 0; field < sizeof(good); field++) {
        for (unsigned value = 0; value < 256; value++) {
            struct ankur_settings s = good;
            ((uint8_t *)&s)[field] = value;
            bool expected = field == 0 ? value == 1 : field < 3 ? value < 20 : value <= 1;
            assert(ankur_settings_valid(&s) == expected);
        }
    }
    for (unsigned level = 0; level < 20; level++) {
        assert(ankur_step(level, true) == (level == 19 ? 19 : level + 1));
        assert(ankur_step(level, false) == (level == 0 ? 0 : level - 1));
    }
    assert(ankur_guarded(POWER_OFF));
    assert(ankur_guarded(CLEAR_ALL));
    assert(ankur_guarded(CLEAR_CURRENT));
    assert(ankur_guarded(RESET_SENSITIVITY));
    assert(!ankur_guarded(SCROLL_TOGGLE));
    assert(!ankur_guarded(ANKUR_ACTION_COUNT));
    return 0;
}
