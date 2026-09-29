#pragma once
#include <nds.h>
#include <stdbool.h>

enum { YE_LINE, YE_OPTIONS, YE_COMMAND, YE_DONE };
#define YARN_MAX_OPT 6

typedef struct {
    int type;
    const char *text;                 // LINE text / COMMAND string
    int nopt;
    const char *opt[YARN_MAX_OPT];    // OPTIONS
} YarnEvent;

bool yarn_load(const char *path);
int yarn_node_count(void);
const char *yarn_node_name(int i);
bool yarn_start(const char *name);
int yarn_step(YarnEvent *e);          // advance until next event
void yarn_choose(int i);
// variables (numbers x100); used by game code as well
int yarn_get(const char *name);
void yarn_set(const char *name, int v);
