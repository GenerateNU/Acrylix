#ifndef LIMIT_SW_H
#define LIMIT_SW_H

#include <stdbool.h>

void limit_sw_init(void);
bool limit_sw_is_pressed(void);
bool limit_sw_triggered(void);
void limit_sw_clear_trigger(void);

#endif /* LIMIT_SW_H */
