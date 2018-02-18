#ifndef NINEDEBUG_H
#define NINEDEBUG_H

#include <stdio.h>

#define DEBUG(...) if (ENABLE_DEBUG) printf(__VA_ARGS__)

#endif
