#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

void config_load(Config *cfg);
void config_save_default(const char *path);

#endif
