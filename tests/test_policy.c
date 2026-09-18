#include <assert.h>

#include "ankur/policy.h"
#include "ankur/timing.h"

int main(void) {
    struct ankur_preferences p = ANKUR_DEFAULTS;

    assert(ankur_preferences_valid(&p));

    p.pointer = 0;
    assert(!ankur_preferences_valid(&p));

    p.pointer = 21;
    assert(!ankur_preferences_valid(&p));

    p.pointer = 7;
    p.version = 255;
    assert(!ankur_preferences_valid(&p));

    assert(
        ankur_level_step(1, false) == 1 &&
        ankur_level_step(20, true) == 20
    );

    assert(ankur_pointer_coeff[6] == 200000);
    assert(ankur_twist_coeff[4] == 166667);

    /* Protected hold timing boundary tests. */
    assert(!ankur_deadline_due(999, 1000));
    assert(ankur_deadline_due(1000, 1000));
    assert(ankur_deadline_due(1001, 1000));

    assert(!ankur_deadline_due(1999, 2000));
    assert(ankur_deadline_due(2000, 2000));
    assert(ankur_deadline_due(2001, 2000));

    for (unsigned i = 0; i <= 100; ++i) {
        struct ankur_pattern r =
            ankur_battery_report(i);

        unsigned represented = 0;

        assert(r.count <= ANKUR_PATTERN_MAX);

        for (unsigned j = 0; j < r.count; j += 2) {
            represented +=
                r.ms[j] == 250 ? 25 : 5;
        }

        assert(represented == i / 5 * 5);
    }

    for (unsigned i = 1; i <= 20; ++i) {
        struct ankur_pattern r =
            ankur_report(i, 5, AK_BLUE);

        unsigned represented = 0;

        for (unsigned j = 0; j < r.count; j += 2) {
            represented +=
                r.ms[j] == 250 ? 5 : 1;
        }

        assert(represented == i);
    }

    assert(
        ankur_battery_report(25).color ==
        AK_ORANGE
    );

    assert(
        ankur_battery_report(50).color ==
        AK_LIGHT_GREEN
    );

    assert(
        ankur_battery_report(75).color ==
        AK_LIGHT_GREEN
    );

    assert(
        ankur_battery_report(76).color ==
        AK_DARK_GREEN
    );

    for (unsigned i = 0; i < AK_EVENT_COUNT; ++i) {
        assert(ankur_patterns[i].count > 0);
        assert(
            ankur_patterns[i].count <=
            ANKUR_PATTERN_MAX
        );

        assert(ankur_patterns[i].count % 2 == 1);

        assert(
            ankur_duration(&ankur_patterns[i]) <=
            2000
        );
    }

    assert(
        ankur_patterns[AK_STANDARD].color ==
        AK_GREEN
    );

    assert(
        ankur_patterns[AK_HIGH_RES].color ==
        AK_RED
    );

    return 0;
}
