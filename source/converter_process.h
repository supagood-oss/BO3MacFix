#pragma once
#include <stddef.h>
/* Returns the child exit status, -1 on launch/I/O failure, -2 on timeout.
 * Wine services may retain output handles; they must not hold up map loading. */
int bo3_run_converter(const char *wine,const char *converter,const char *input,
    const char *output,unsigned timeout_ms,char *log,size_t log_capacity);
