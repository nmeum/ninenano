#ifndef NINEDEBUG_H
#define NINEDEBUG_H

#include <stdio.h>

#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#if ENABLE_DEBUG
  #define DEBUG(...) DEBUG_PRINT(__VA_ARGS__)
#else
  #define DEBUG(...)
#endif

#endif
