#include <assert.h>
#include <limits.h>
#include "ankur/policy.h"

int main(void) {
    struct ankur_preferences p = ANKUR_DEFAULTS;
    assert(ankur_preferences_valid(&p));
    p.pointer=0; assert(!ankur_preferences_valid(&p));
    p.pointer=21; assert(!ankur_preferences_valid(&p));
    p.pointer=7; p.version=255; assert(!ankur_preferences_valid(&p));
    assert(ankur_level_step(1,false)==1 && ankur_level_step(20,true)==20);
    assert(ankur_pointer_coeff[6]==200000 && ankur_twist_coeff[4]==166667);
    for (int level=0; level<20; ++level) {
        int64_t rem=0, total=0;
        for (int i=0; i<1000; ++i) total+=ankur_scale(1,ankur_pointer_coeff[level],&rem);
        assert(total*1000000+rem==(int64_t)1000*ankur_pointer_coeff[level]);
        for (int i=0; i<1000; ++i) total+=ankur_scale(-1,ankur_pointer_coeff[level],&rem);
        assert(total==0 && rem==0);
    }
    int64_t rem=0;
    assert(ankur_scale(INT32_MAX,1000000,&rem)==INT32_MAX);
    assert(ankur_scale(INT32_MIN,1000000,&rem)==INT32_MIN);
    for (unsigned i=0; i<=100; ++i) {
        struct ankur_pattern r=ankur_battery_report(i);
        unsigned represented=0;
        assert(r.count<=ANKUR_PATTERN_MAX);
        for (unsigned j=0;j<r.count;j+=2) represented+=r.ms[j]==250?25:5;
        assert(represented==i/5*5);
    }
    for (unsigned i=1;i<=20;++i) {
        struct ankur_pattern r=ankur_report(i,5,AK_BLUE);
        unsigned represented=0;
        for (unsigned j=0;j<r.count;j+=2) represented+=r.ms[j]==250?5:1;
        assert(represented==i);
    }
    assert(ankur_battery_report(25).color==AK_ORANGE);
    assert(ankur_battery_report(50).color==AK_LIGHT_GREEN);
    assert(ankur_battery_report(75).color==AK_LIGHT_GREEN);
    assert(ankur_battery_report(76).color==AK_DARK_GREEN);
    for (unsigned i=0;i<AK_EVENT_COUNT;++i) {
        assert(ankur_patterns[i].count>0 && ankur_patterns[i].count<=ANKUR_PATTERN_MAX);
        assert(ankur_patterns[i].count%2==1);
        assert(ankur_duration(&ankur_patterns[i])<=2000);
    }
    assert(ankur_patterns[AK_STANDARD].color==AK_GREEN);
    assert(ankur_patterns[AK_HIGH_RES].color==AK_RED);
    return 0;
}
