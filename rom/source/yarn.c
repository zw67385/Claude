#include "yarn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

enum { OP_JUMP_TO, OP_JUMP, OP_RUN_LINE, OP_RUN_COMMAND, OP_ADD_OPTION, OP_SHOW_OPTIONS, OP_PUSH_STRING,
       OP_PUSH_FLOAT, OP_PUSH_BOOL, OP_PUSH_NULL, OP_JUMP_IF_FALSE, OP_POP, OP_CALL_FUNC, OP_PUSH_VARIABLE,
       OP_STORE_VARIABLE, OP_STOP, OP_RUN_NODE };

typedef struct { u8 op, p0; u16 p1; s32 a, b; } Inst;
typedef struct { u32 name, first; } Node;
typedef struct { u32 ninst, nnodes, nstr, insoff, nodeoff, stroff, blobsize; } Hdr;
typedef struct { int t; s32 v; } Val;   // t: 0 num(x100) 1 str idx 2 bool 3 inst target

static u8 *data;
static Hdr *hdr;
static Inst *ins;
static Node *nodes;
static u32 *stroffs;
static const char *blob;

#define VARS 96
static struct { int name; Val v; } vars[VARS];
static int nvars;
static Val stack[64];
static int sp;
static int pc = -1;
static struct { const char *text; int dest; } opts[YARN_MAX_OPT];
static int nopts;

static const char *str(int i) { return blob + stroffs[i]; }

bool yarn_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    data = memalign(4, sz);
    if (!data || fread(data, 1, sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);
    hdr = (Hdr *)(data + 4);
    ins = (Inst *)(data + hdr->insoff);
    nodes = (Node *)(data + hdr->nodeoff);
    stroffs = (u32 *)(data + hdr->stroff);
    blob = (const char *)(data + hdr->stroff + 4 * hdr->nstr);
    return true;
}

int yarn_node_count(void) { return hdr ? (int)hdr->nnodes : 0; }
const char *yarn_node_name(int i) { return str(nodes[i].name); }

bool yarn_start(const char *name)
{
    for (u32 i = 0; i < hdr->nnodes; i++)
        if (!strcmp(str(nodes[i].name), name)) { pc = nodes[i].first; sp = 0; nopts = 0; return true; }
    return false;
}

static Val *var(const char *name, bool create)
{
    for (int i = 0; i < nvars; i++) if (!strcmp(str(vars[i].name), name)) return &vars[i].v;
    if (!create) return NULL;
    for (u32 i = 0; i < hdr->nstr; i++)
        if (!strcmp(str(i), name)) { vars[nvars].name = i; vars[nvars].v = (Val){0, 0}; return &vars[nvars++].v; }
    return NULL;
}
int yarn_get(const char *n) { Val *v = var(n, false); return v ? v->v : 0; }
void yarn_set(const char *n, int x) { Val *v = var(n, true); if (v) { v->t = 0; v->v = x; } }

static bool truthy(Val v) { return v.v != 0; }

int yarn_step(YarnEvent *e)
{
    while (pc >= 0) {
        Inst *i = &ins[pc++];
        switch (i->op) {
        case OP_JUMP_TO: pc = i->a; break;
        case OP_JUMP: if (sp) pc = stack[--sp].v; break;
        case OP_RUN_LINE: e->type = YE_LINE; e->text = str(i->a); return YE_LINE;
        case OP_RUN_COMMAND: e->type = YE_COMMAND; e->text = str(i->a); return YE_COMMAND;
        case OP_ADD_OPTION:
            if (nopts < YARN_MAX_OPT) { opts[nopts].text = str(i->a); opts[nopts].dest = i->b; nopts++; }
            break;
        case OP_SHOW_OPTIONS:
            if (!nopts) { pc = -1; e->type = YE_DONE; return YE_DONE; }
            e->type = YE_OPTIONS; e->nopt = nopts;
            for (int k = 0; k < nopts; k++) e->opt[k] = opts[k].text;
            return YE_OPTIONS;
        case OP_PUSH_STRING: stack[sp++] = (Val){1, i->a}; break;
        case OP_PUSH_FLOAT: stack[sp++] = (Val){0, i->a}; break;
        case OP_PUSH_BOOL: stack[sp++] = (Val){2, i->a}; break;
        case OP_PUSH_NULL: stack[sp++] = (Val){0, 0}; break;
        case OP_JUMP_IF_FALSE: if (sp && !truthy(stack[sp - 1])) pc = i->a; break;
        case OP_POP: if (sp) sp--; break;
        case OP_PUSH_VARIABLE: { Val *v = var(str(i->a), true); stack[sp++] = v ? *v : (Val){0, 0}; break; }
        case OP_STORE_VARIABLE: { Val *v = var(str(i->a), true); if (v && sp) *v = stack[sp - 1]; break; }
        case OP_CALL_FUNC: {
            const char *fn = str(i->a);
            int n = sp ? stack[--sp].v / 100 : 0;
            Val r = {0, 0};
            if (!strcmp(fn, "Number.Add") && n == 2) { r.v = stack[sp - 2].v + stack[sp - 1].v; }
            else if (!strcmp(fn, "Number.EqualTo") || !strcmp(fn, "Bool.EqualTo")) {
                r.t = 2; r.v = n == 2 && stack[sp - 2].v == stack[sp - 1].v;
            }
            sp -= n; if (sp < 0) sp = 0;
            stack[sp++] = r; break;
        }
        case OP_STOP: pc = -1; e->type = YE_DONE; return YE_DONE;
        case OP_RUN_NODE: {
            int idx = sp ? stack[--sp].v : -1;
            if (idx < 0 || !yarn_start(str(idx))) { pc = -1; e->type = YE_DONE; return YE_DONE; }
            break;
        }
        }
    }
    e->type = YE_DONE;
    return YE_DONE;
}

void yarn_choose(int i)
{
    if (i < 0 || i >= nopts) i = 0;
    stack[sp++] = (Val){3, opts[i].dest};
    nopts = 0;
}
