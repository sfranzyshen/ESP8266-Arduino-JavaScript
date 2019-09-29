// Copyright (c) 2013-2019 Cesanta Software Limited
// All rights reserved
//
// This software is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation. For the terms of this
// license, see <http://www.gnu.org/licenses/>.
//
// You are free to use this software under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this software under a commercial
// license, please contact us at https://mdash.net/home/company.html


#ifndef MJS_DATA_STACK_SIZE
#define MJS_DATA_STACK_SIZE 10
#endif

#ifndef MJS_CALL_STACK_SIZE
#define MJS_CALL_STACK_SIZE 10
#endif

#ifndef MJS_STRING_POOL_SIZE
#define MJS_STRING_POOL_SIZE 256
#endif

#ifndef MJS_OBJ_POOL_SIZE
#define MJS_OBJ_POOL_SIZE 5
#endif

#ifndef MJS_PROP_POOL_SIZE
#define MJS_PROP_POOL_SIZE 10
#endif

#ifndef MJS_CFUNC_POOL_SIZE
#define MJS_CFUNC_POOL_SIZE 5
#endif

#ifndef MJS_ERROR_MESSAGE_SIZE
#define MJS_ERROR_MESSAGE_SIZE 40
#endif

#ifndef MJS_H
#define MJS_H

#if defined(__cplusplus)
extern "C" {
#endif

#if !defined(_MSC_VER) || _MSC_VER >= 1700
#include <stdbool.h>
#include <stdint.h>
#else
typedef int bool;
enum { false = 0, true = 1 };
typedef long intptr_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
#define vsnprintf _vsnprintf
#define snprintf _snprintf
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define __func__ __FILE__ ":" STRINGIFY(__LINE__)
#pragma warning(disable : 4127)
#endif

#define mjs vm  // Aliasing `struct mjs` to `struct vm`

typedef uint32_t mjs_val_t;         // JS value placeholder
typedef uint32_t mjs_len_t;         // String length placeholder
typedef void (*mjs_cfn_t)(void);    // Native C function, for exporting to JS
// typedef enum { CT_FLOAT = 0, CT_CHAR_PTR = 1 } mjs_ctype_t;  // C FFI types

typedef mjs_val_t val_t;
typedef mjs_len_t len_t;
typedef mjs_cfn_t cfn_t;
typedef uint16_t ind_t;
typedef uint32_t tok_t;
#define INVALID_INDEX ((ind_t) ~0)

static struct mjs *mjs_create(void);            // Create instance
static void mjs_destroy(struct mjs *);          // Destroy instance
val_t mjs_get_global(struct mjs *);      // Get global namespace object
static val_t mjs_eval(struct mjs *, const char *buf, int len);  // Evaluate expr
static val_t mjs_set(struct vm *, val_t obj, val_t key, val_t val);  // Set attribute
const char *mjs_stringify(struct mjs *, val_t v);             // Stringify value
unsigned long mjs_size(void);                          // Get VM size

// Converting from C type to val_t
// Use MJS_UNDEFINED, MJS_NULL, MJS_TRUE, MJS_FALSE for other scalar types
val_t mjs_mk_obj(struct mjs *);
val_t mjs_mk_str(struct mjs *, const char *, int len);
val_t mjs_mk_num(float value);
val_t mjs_mk_js_func(struct mjs *, const char *, int len);

// Converting from val_t to C/C++ types
float mjs_to_float(val_t v);                         // Unpack number
static char *mjs_to_str(struct mjs *, val_t, len_t *);      // Unpack string

#define mjs_to_float(v) tof(v)
#define mjs_mk_str(vm, s, n) mk_str(vm, s, n)
#define mjs_mk_obj(vm) mk_obj(vm)
#define mjs_mk_num(v) tov(v)
#define mjs_get_global(vm) ((vm)->call_stack[0])
#define mjs_stringify(vm, v) tostr(vm, v)

#if defined(__cplusplus)
}
#endif

// VM tunables


///////////////////////////////// IMPLEMENTATION //////////////////////////
//
// 32bit floating-point number: 1 bit sign, 8 bits exponent, 23 bits mantissa
//
//  7f80 0000 = 01111111 10000000 00000000 00000000 = infinity
//  ff80 0000 = 11111111 10000000 00000000 00000000 = −infinity
//  ffc0 0001 = x1111111 11000000 00000000 00000001 = qNaN (on x86 and ARM)
//  ff80 0001 = x1111111 10000000 00000000 00000001 = sNaN (on x86 and ARM)
//
//  seeeeeee|emmmmmmm|mmmmmmmm|mmmmmmmm
//  11111111|1ttttvvv|vvvvvvvv|vvvvvvvv
//    INF     TYPE     PAYLOAD

#define IS_FLOAT(v) (((v) &0xff800000) != 0xff800000)
#define MK_VAL(t, p) (0xff800000 | ((val_t)(t) << 19) | (p))
#define VAL_TYPE(v) ((mjs_type_t)(((v) >> 19) & 0x0f))
#define VAL_PAYLOAD(v) ((v) & ~0xfff80000)

#define MJS_UNDEFINED MK_VAL(MJS_TYPE_UNDEFINED, 0)
#define MJS_ERROR MK_VAL(MJS_TYPE_ERROR, 0)
#define MJS_TRUE MK_VAL(MJS_TYPE_TRUE, 0)
#define MJS_FALSE MK_VAL(MJS_TYPE_FALSE, 0)
#define MJS_NULL MK_VAL(MJS_TYPE_NULL, 0)

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
typedef enum {
  MJS_TYPE_UNDEFINED, MJS_TYPE_NULL, MJS_TYPE_TRUE, MJS_TYPE_FALSE,
  MJS_TYPE_STRING, MJS_TYPE_OBJECT, MJS_TYPE_ARRAY, MJS_TYPE_FUNCTION,
  MJS_TYPE_NUMBER, MJS_TYPE_ERROR, MJS_TYPE_C_FUNCTION,
} mjs_type_t;
// clang-format on

struct prop {
  val_t key;
  val_t val;
  ind_t flags;  // see MJS_PROP_* below
  ind_t next;   // index of the next prop, or INVALID_INDEX if last one
};
#define PROP_ALLOCATED 1

struct obj {
  ind_t flags;  // see MJS_OBJ_* defines below
  ind_t props;  // index of the first property, or INVALID_INDEX
};
#define OBJ_ALLOCATED 1
#define OBJ_CALL_ARGS 2  // This oject sits in the call stack, holds call args

struct cfunc {
  cfn_t fn;           // Pointer to C function
  const char *decl;   // Declaration of return values and arguments
};

struct vm {
  char error_message[MJS_ERROR_MESSAGE_SIZE];
  val_t data_stack[MJS_DATA_STACK_SIZE];
  val_t call_stack[MJS_CALL_STACK_SIZE];
  ind_t sp;                               // Points to the top of the data stack
  ind_t csp;                              // Points to the top of the call stack
  ind_t stringbuf_len;                    // String pool current length
  struct obj objs[MJS_OBJ_POOL_SIZE];     // Objects pool
  struct prop props[MJS_PROP_POOL_SIZE];  // Props pool
  struct cfunc cfuncs[MJS_CFUNC_POOL_SIZE];  // C functions pool
  uint8_t stringbuf[MJS_STRING_POOL_SIZE];   // String pool
};

#define ARRSIZE(x) ((sizeof(x) / sizeof((x)[0])))

#define DBGPREFIX "[DEBUG] "
#ifdef MJS_DEBUG
#define LOG(x) printf x
#else
#define LOG(x)
#endif

#define TRY(expr)                     \
  do {                                \
    res = expr;                       \
    if (res == MJS_ERROR) return res; \
  } while (0)

//////////////////////////////////// HELPERS /////////////////////////////////
static val_t vm_err(struct vm *vm, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->error_message, sizeof(vm->error_message), fmt, ap);
  va_end(ap);
  // LOG((DBGPREFIX "%s: %s\n", __func__, vm->error_message));
  return MJS_ERROR;
}

union mjs_type_holder {
  val_t v;
  float f;
};

static mjs_type_t mjs_type(val_t v) {
  return IS_FLOAT(v) ? MJS_TYPE_NUMBER : VAL_TYPE(v);
}

static val_t tov(float f) {
  union mjs_type_holder u;
  u.f = f;
  return u.v;
}

static float tof(val_t v) {
  union mjs_type_holder u;
  u.v = v;
  return u.f;
}

static const char *mjs_typeof(val_t v) {
  const char *names[] = {"undefined", "null",   "true",   "false",
                         "string",    "object", "object", "function",
                         "number",    "error",  "cfunc",  "?",
                         "?",         "?",      "?",      "?"};
  return names[mjs_type(v)];
}

static struct prop *firstprop(struct vm *vm, val_t obj);
const char *tostr(struct vm *vm, val_t v) {
  static char buf[64];
  mjs_type_t t = mjs_type(v);
  switch (t) {
    case MJS_TYPE_NUMBER: {
      double f = tof(v), iv;
      if (modf(f, &iv) == 0) {
        snprintf(buf, sizeof(buf), "%ld", (long) f);
      } else {
        snprintf(buf, sizeof(buf), "%g", f);
      }
      break;
    }
    case MJS_TYPE_STRING:
    case MJS_TYPE_FUNCTION: {
      len_t len;
      const char *ptr = mjs_to_str(vm, v, &len);
      snprintf(buf, sizeof(buf), "%.*s", len, ptr);
      break;
    }
    case MJS_TYPE_C_FUNCTION:
      snprintf(buf, sizeof(buf), "cfunc@%p", vm->cfuncs[VAL_PAYLOAD(v)].fn);
      break;
    case MJS_TYPE_ERROR:
      snprintf(buf, sizeof(buf), "ERROR: %s", vm->error_message);
      break;
    case MJS_TYPE_OBJECT: {
      int n = snprintf(buf, sizeof(buf), "obj(");
      struct prop *prop = firstprop(vm, v);
      while (prop != NULL) {
        char *key = mjs_to_str(vm, prop->key, NULL);
        n += snprintf(buf + n, sizeof(buf) - n, "%s%s", n > 4 ? "," : "", key);
        prop = prop->next == INVALID_INDEX ? NULL : &vm->props[prop->next];
      }
      n += snprintf(buf + n, sizeof(buf) - n, ")");
      break;
    }
    default:
      snprintf(buf, sizeof(buf), "%s", mjs_typeof(v));
      break;
  }
  return buf;
}

#ifdef MJS_DEBUG
static void vm_dump(const struct vm *vm) {
  ind_t i;
  printf("[VM] %8s: ", "objs");
  for (i = 0; i < ARRSIZE(vm->objs); i++) {
    putchar(vm->objs[i].flags ? 'v' : '-');
  }
  putchar('\n');
  printf("[VM] %8s: ", "props");
  for (i = 0; i < ARRSIZE(vm->props); i++) {
    putchar(vm->props[i].flags ? 'v' : '-');
  }
  putchar('\n');
  printf("[VM] %8s: ", "cfuncs");
  for (i = 0; i < ARRSIZE(vm->cfuncs); i++) {
    putchar(vm->cfuncs[i].fn ? 'v' : '-');
  }
  putchar('\n');
  printf("[VM] %8s: %d/%d\n", "strings", vm->stringbuf_len,
         (int) sizeof(vm->stringbuf));
  printf("[VM]  sp %d, csp %d, sb %d\n", vm->sp, vm->csp, vm->stringbuf_len);
}
#else
#define vm_dump(x)
#endif

////////////////////////////////////// VM ////////////////////////////////////
static val_t *vm_top(struct vm *vm) { return &vm->data_stack[vm->sp - 1]; }

static void abandon(struct vm *vm, val_t v) {
  mjs_type_t t = mjs_type(v);
  LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, v)));

  if (t != MJS_TYPE_OBJECT && t != MJS_TYPE_STRING) return;
  {
    ind_t j;
    // If this value is still referenced, do nothing
    for (j = 0; j < ARRSIZE(vm->props); j++) {
      struct prop *prop = &vm->props[j];
      if (prop->flags == 0) continue;
      if (v == prop->key || v == prop->val) return;
    }
    // Look at the data stack too
    for (j = 0; j < vm->sp; j++)
      if (v == vm->data_stack[j]) return;
  }

  // vm_dump(vm);
  if (t == MJS_TYPE_OBJECT) {
    ind_t i, obj_index = (ind_t) VAL_PAYLOAD(v);
    struct obj *o = &vm->objs[obj_index];
    o->flags = 0;  // Mark object free
    i = o->props;
    while (i != INVALID_INDEX) {  // Deallocate obj's properties too
      struct prop *prop = &vm->props[i];
      prop->flags = 0;  // Mark property free
      assert(mjs_type(prop->key) == MJS_TYPE_STRING);
      abandon(vm, prop->key);
      abandon(vm, prop->val);
      i = prop->next;  // Point to the next property
    }
  } else if (t == MJS_TYPE_STRING) {
    ind_t k, j, i = (ind_t) VAL_PAYLOAD(v);     // String begin
    ind_t len = (ind_t)(vm->stringbuf[i] + 2);  // String length

    // printf("abandoning %d %d [%s]\n", (int) i, (int) len, tostr(vm, v));
    // Ok, not referenced, deallocate a string
    if (i + len == sizeof(vm->stringbuf) || i + len == vm->stringbuf_len) {
      // printf("shrink [%s]\n", tostr(vm, v));
      vm->stringbuf[i] = 0;   // If we're the last string,
      vm->stringbuf_len = i;  // shrink the buf immediately
    } else {
      vm->stringbuf[i + len - 1] = 'x';  // Mark string as dead
      // Relocate all live strings to the beginning of the buffer
      // printf("--> RELOC, %hu %hu\n", vm->stringbuf_len, len);
      memmove(&vm->stringbuf[i], &vm->stringbuf[i + len], len);
      assert(vm->stringbuf_len >= len);
      vm->stringbuf_len = (ind_t)(vm->stringbuf_len - len);
      for (j = 0; j < ARRSIZE(vm->props); j++) {
        struct prop *prop = &vm->props[j];
        if (prop->flags != 0) continue;
        k = (ind_t) VAL_PAYLOAD(prop->key);
        if (k > i) prop->key = MK_VAL(MJS_TYPE_STRING, k - len);
        if (mjs_type(prop->val) == MJS_TYPE_STRING) {
          k = (ind_t) VAL_PAYLOAD(prop->val);
          if (k > i) prop->key = MK_VAL(MJS_TYPE_STRING, k - len);
        }
      }
    }
    // printf("sbuflen %d\n", (int) vm->stringbuf_len);
  }
}

static val_t vm_push(struct vm *vm, val_t v) {
  if (vm->sp < ARRSIZE(vm->data_stack)) {
    LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, v)));
    vm->data_stack[vm->sp] = v;
    vm->sp++;
    return MJS_TRUE;
  } else {
    return vm_err(vm, "stack overflow");
  }
}

static val_t vm_drop(struct vm *vm) {
  if (vm->sp > 0) {
    LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, *vm_top(vm))));
    vm->sp--;
    abandon(vm, vm->data_stack[vm->sp]);
    return MJS_TRUE;
  } else {
    return vm_err(vm, "stack underflow");
  }
}

static val_t mk_str(struct vm *vm, const char *p, int n) {
  len_t len = n < 0 ? (len_t) strlen(p) : (len_t) n;
  // printf("%s [%.*s], %d\n", __func__, n, p, (int) vm->stringbuf_len);
  if (len > 0xff) {
    return vm_err(vm, "string is too long");
  } else if (len + 2 > sizeof(vm->stringbuf) - vm->stringbuf_len) {
    return vm_err(vm, "string OOM");
  } else {
    val_t v = MK_VAL(MJS_TYPE_STRING, vm->stringbuf_len);
    vm->stringbuf[vm->stringbuf_len++] = (uint8_t)(len & 0xff);  // save length
    if (p) memmove(&vm->stringbuf[vm->stringbuf_len], p, len);   // copy data
    vm->stringbuf_len = (ind_t)(vm->stringbuf_len + len);
    vm->stringbuf[vm->stringbuf_len++] = 0;  // nul-terminate
    return v;
  }
}

static char *mjs_to_str(struct vm *vm, val_t v, len_t *len) {
  uint8_t *p = vm->stringbuf + VAL_PAYLOAD(v);
  if (len != NULL) *len = p[0];
  return (char *) p + 1;
}

static val_t mjs_concat(struct vm *vm, val_t v1, val_t v2) {
  val_t v = MJS_ERROR;
  len_t n1, n2;
  char *p1 = mjs_to_str(vm, v1, &n1), *p2 = mjs_to_str(vm, v2, &n2);
  if ((v = mk_str(vm, NULL, n1 + n2)) != MJS_ERROR) {
    char *p = mjs_to_str(vm, v, NULL);
    memmove(p, p1, n1);
    memmove(p + n1, p2, n2);
  }
  return v;
}

static val_t mk_cfunc(struct vm *vm) {
  ind_t i;
  for (i = 0; i < ARRSIZE(vm->cfuncs); i++) {
    if (vm->cfuncs[i].fn != NULL) continue;
    return MK_VAL(MJS_TYPE_C_FUNCTION, i);
  }
  return vm_err(vm, "cfunc OOM");
}

static val_t mk_obj(struct vm *vm) {
  ind_t i;
  // Start iterating from 1, because object 0 is always a global object
  for (i = 1; i < ARRSIZE(vm->objs); i++) {
    if (vm->objs[i].flags != 0) continue;
    vm->objs[i].flags = OBJ_ALLOCATED;
    vm->objs[i].props = INVALID_INDEX;
    return MK_VAL(MJS_TYPE_OBJECT, i);
  }
  return vm_err(vm, "obj OOM");
}

static val_t mk_func(struct vm *vm, const char *code, int len) {
  val_t v = mk_str(vm, code, len);
  if (v != MJS_ERROR) {
    v &= ~((val_t) 0x0f << 19);
    v |= (val_t) MJS_TYPE_FUNCTION << 19;
  }
  return v;
}

static val_t create_scope(struct vm *vm) {
  val_t scope;
  if (vm->csp >= ARRSIZE(vm->call_stack) - 1) {
    return vm_err(vm, "Call stack OOM");
  }
  if ((scope = mk_obj(vm)) == MJS_ERROR) return MJS_ERROR;
  LOG((DBGPREFIX "%s\n", __func__));
  vm->call_stack[vm->csp] = scope;
  vm->csp++;
  return scope;
}

static val_t delete_scope(struct vm *vm) {
  if (vm->csp <= 0 || vm->csp >= ARRSIZE(vm->call_stack)) {
    return vm_err(vm, "Corrupt call stack");
  } else {
    LOG((DBGPREFIX "%s\n", __func__));
    vm->csp--;
    abandon(vm, vm->call_stack[vm->csp]);
    return MJS_TRUE;
  }
}

static struct prop *firstprop(struct vm *vm, val_t obj) {
  ind_t obj_index = (ind_t) VAL_PAYLOAD(obj);
  struct obj *o = &vm->objs[obj_index];
  if (obj_index >= ARRSIZE(vm->objs)) return NULL;
  return o->props == INVALID_INDEX ? NULL : &vm->props[o->props];
}

// Lookup property in a given object
static val_t *findprop(struct vm *vm, val_t obj, const char *ptr, len_t len) {
  struct prop *prop = firstprop(vm, obj);
  while (prop != NULL) {
    len_t n = 0;
    char *key = mjs_to_str(vm, prop->key, &n);
    if (n == len && memcmp(key, ptr, n) == 0) return &prop->val;
    prop = prop->next == INVALID_INDEX ? NULL : &vm->props[prop->next];
  }
  return NULL;
}

// Lookup variable
static val_t *lookup(struct vm *vm, const char *ptr, len_t len) {
  ind_t i;
  for (i = vm->csp; i > 0; i--) {
    val_t scope = vm->call_stack[i - 1];
    val_t *prop = findprop(vm, scope, ptr, len);
    // printf(" lookup scope %d %s [%.*s] %p\n", (int) i, tostr(vm, scope),
    //(int) len, ptr, prop);
    if (prop != NULL) return prop;
  }
  return NULL;
}

// Lookup variable and push its value on stack on success
static val_t lookup_and_push(struct vm *vm, const char *ptr, len_t len) {
  val_t *vp = lookup(vm, ptr, len);
  if (vp != NULL) return vm_push(vm, *vp);
  return vm_err(mjs, "[%.*s] undefined", len, ptr);
}

static val_t mjs_set(struct vm *vm, val_t obj, val_t key, val_t val) {
  if (mjs_type(obj) == MJS_TYPE_OBJECT) {
    len_t len;
    const char *ptr = mjs_to_str(vm, key, &len);
    val_t *prop = findprop(vm, obj, ptr, len);
    if (prop != NULL) {
      *prop = val;
      return MJS_TRUE;
    } else {
      ind_t i, obj_index = (ind_t) VAL_PAYLOAD(obj);
      struct obj *o = &vm->objs[obj_index];
      if (obj_index >= ARRSIZE(vm->objs)) {
        return vm_err(vm, "corrupt obj, index %x", obj_index);
      }
      for (i = 0; i < ARRSIZE(vm->props); i++) {
        struct prop *p = &vm->props[i];
        if (p->flags != 0) continue;
        p->flags = PROP_ALLOCATED;
        p->next = o->props;  // Link to the current
        o->props = i;        // props list
        p->key = key;
        p->val = val;
        LOG((DBGPREFIX "%s: prop %hu %s -> ", __func__, i, tostr(vm, key)));
        LOG(("%s\n", tostr(vm, val)));
        return MJS_TRUE;
      }
      return vm_err(vm, "props OOM");
    }
  } else {
    return vm_err(vm, "setting prop on non-object");
  }
}

static int is_true(struct vm *vm, val_t v) {
  len_t len;
  mjs_type_t t = mjs_type(v);
  return t == MJS_TYPE_TRUE || (t == MJS_TYPE_NUMBER && tof(v) != 0.0) ||
         t == MJS_TYPE_OBJECT || t == MJS_TYPE_FUNCTION ||
         (t == MJS_TYPE_STRING && mjs_to_str(vm, v, &len) && len > 0);
}

////////////////////////////////// TOKENIZER /////////////////////////////////

struct tok {
  tok_t tok, len;
  const char *ptr;
  float num_value;
};

struct parser {
  const char *file_name;  // Source code file name
  const char *buf;        // Nul-terminated source code buffer
  const char *pos;        // Current position
  const char *end;        // End position
  int line_no;            // Line number
  tok_t prev_tok;         // Previous token, for prefix increment / decrement
  struct tok tok;         // Parsed token
  int noexec;             // Parse only, do not execute
  struct vm *vm;
};

#define DT(a, b) ((tok_t)(a) << 8 | (b))
#define TT(a, b, c) ((tok_t)(a) << 16 | (tok_t)(b) << 8 | (c))
#define QT(a, b, c, d) \
  ((tok_t)(a) << 24 | (tok_t)(b) << 16 | (tok_t)(c) << 8 | (d))

// clang-format off
enum {
  TOK_EOF, TOK_INVALID, TOK_NUM, TOK_STR, TOK_IDENT = 200,
  TOK_BREAK, TOK_CASE, TOK_CATCH, TOK_CONTINUE, TOK_DEBUGGER, TOK_DEFAULT,
  TOK_DELETE, TOK_DO, TOK_ELSE, TOK_FALSE, TOK_FINALLY, TOK_FOR, TOK_FUNCTION,
  TOK_IF, TOK_IN, TOK_INSTANCEOF, TOK_NEW, TOK_NULL, TOK_RETURN, TOK_SWITCH,
  TOK_THIS, TOK_THROW, TOK_TRUE, TOK_TRY, TOK_TYPEOF, TOK_VAR, TOK_VOID,
  TOK_WHILE, TOK_WITH, TOK_LET, TOK_UNDEFINED,
  TOK_UNARY_MINUS, TOK_UNARY_PLUS, TOK_POSTFIX_PLUS, TOK_POSTFIX_MINUS,
};
// clang-format on

// We're not relying on the target libc ctype, as it may incorrectly
// handle negative arguments, e.g. isspace(-1).
static int mjs_is_space(int c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' ||
         c == '\v';
}

static int mjs_is_digit(int c) {
  return c >= '0' && c <= '9';
}

static int mjs_is_alpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int mjs_is_ident(int c) {
  return c == '_' || c == '$' || mjs_is_alpha(c);
}

// Try to parse a token that can take one or two chars.
static int longtok(struct parser *p, const char *first_chars,
                   const char *second_chars) {
  if (strchr(first_chars, p->pos[0]) == NULL) return TOK_EOF;
  if (p->pos + 1 < p->end && p->pos[1] != '\0' &&
      strchr(second_chars, p->pos[1]) != NULL) {
    p->tok.len++;
    p->pos++;
    return p->pos[-1] << 8 | p->pos[0];
  }
  return p->pos[0];
}

// Try to parse a token that takes exactly 3 chars.
static int longtok3(struct parser *p, char a, char b, char c) {
  if (p->pos + 2 < p->end && p->pos[0] == a && p->pos[1] == b &&
      p->pos[2] == c) {
    p->tok.len += 2;
    p->pos += 2;
    return ((tok_t) p->pos[-2] << 16) | ((tok_t) p->pos[-1] << 8) | p->pos[0];
  }
  return TOK_EOF;
}

// Try to parse a token that takes exactly 4 chars.
static int longtok4(struct parser *p, char a, char b, char c, char d) {
  if (p->pos + 3 < p->end && p->pos[0] == a && p->pos[1] == b &&
      p->pos[2] == c && p->pos[3] == d) {
    p->tok.len += 3;
    p->pos += 3;
    return ((tok_t) p->pos[-3] << 24) | ((tok_t) p->pos[-2] << 16) |
           ((tok_t) p->pos[-1] << 8) | p->pos[0];
  }
  return TOK_EOF;
}

static int getnum(struct parser *p) {
  if (p->pos[0] == '0' && p->pos[1] == 'x') {
    // MSVC6 strtod cannot parse 0x... numbers, thus this ugly workaround.
    p->tok.num_value = (float) strtoul(p->pos + 2, (char **) &p->pos, 16);
  } else {
    p->tok.num_value = (float) strtod(p->pos, (char **) &p->pos);
  }
  p->tok.len = p->pos - p->tok.ptr;
  p->pos--;
  return TOK_NUM;
}

static int is_reserved_word_token(const char *s, int len) {
  const char *reserved[] = {
      "break",     "case",   "catch", "continue",   "debugger", "default",
      "delete",    "do",     "else",  "false",      "finally",  "for",
      "function",  "if",     "in",    "instanceof", "new",      "null",
      "return",    "switch", "this",  "throw",      "true",     "try",
      "typeof",    "var",    "void",  "while",      "with",     "let",
      "undefined", NULL};
  int i;
  if (!mjs_is_alpha(s[0])) return 0;
  for (i = 0; reserved[i] != NULL; i++) {
    if (len == (int) strlen(reserved[i]) && strncmp(s, reserved[i], len) == 0)
      return i + 1;
  }
  return 0;
}

static int getident(struct parser *p) {
  while (mjs_is_ident(p->pos[0]) || mjs_is_digit(p->pos[0])) p->pos++;
  p->tok.len = p->pos - p->tok.ptr;
  p->pos--;
  return TOK_IDENT;
}

static int getstr(struct parser *p) {
  int quote = *p->pos++;
  p->tok.ptr++;
  while (p->pos[0] != '\0' && p->pos < p->end && p->pos[0] != quote) {
    if (p->pos[0] == '\\' && p->pos[1] != '\0' &&
        (p->pos[1] == quote || strchr("bfnrtv\\", p->pos[1]) != NULL)) {
      p->pos += 2;
    } else {
      p->pos++;
    }
  }
  p->tok.len = p->pos - p->tok.ptr;
  return TOK_STR;
}

static void skip_spaces_and_comments(struct parser *p) {
  const char *pos;
  do {
    pos = p->pos;
    while (p->pos < p->end && mjs_is_space(p->pos[0])) {
      if (p->pos[0] == '\n') p->line_no++;
      p->pos++;
    }
    if (p->pos + 1 < p->end && p->pos[0] == '/' && p->pos[1] == '/') {
      while (p->pos[0] != '\0' && p->pos[0] != '\n') p->pos++;
    }
    if (p->pos + 4 < p->end && p->pos[0] == '/' && p->pos[1] == '*') {
      p->pos += 2;
      while (p->pos < p->end && p->pos[0] != '\0') {
        if (p->pos[0] == '\n') p->line_no++;
        if (p->pos + 1 < p->end && p->pos[0] == '*' && p->pos[1] == '/') {
          p->pos += 2;
          break;
        }
        p->pos++;
      }
    }
  } while (pos < p->pos);
}

static tok_t pnext(struct parser *p) {
  tok_t tmp, tok = TOK_INVALID;

  skip_spaces_and_comments(p);
  p->tok.ptr = p->pos;
  p->tok.len = 1;

  if (p->pos[0] == '\0' || p->pos >= p->end) tok = TOK_EOF;
  if (mjs_is_digit(p->pos[0])) {
    tok = getnum(p);
  } else if (p->pos[0] == '\'' || p->pos[0] == '"') {
    tok = getstr(p);
  } else if (mjs_is_ident(p->pos[0])) {
    tok = getident(p);
    // NOTE: getident() has side effects on `p`, and `is_reserved_word_token()`
    // relies on them. Since in C the order of evaluation of the operands is
    // undefined, `is_reserved_word_token()` should be called in a separate
    // statement.
    tok += is_reserved_word_token(p->tok.ptr, p->tok.len);
  } else if (strchr(",.:;{}[]()?", p->pos[0]) != NULL) {
    tok = p->pos[0];
  } else if ((tmp = longtok3(p, '<', '<', '=')) != TOK_EOF ||
             (tmp = longtok3(p, '>', '>', '=')) != TOK_EOF ||
             (tmp = longtok4(p, '>', '>', '>', '=')) != TOK_EOF ||
             (tmp = longtok3(p, '>', '>', '>')) != TOK_EOF ||
             (tmp = longtok3(p, '=', '=', '=')) != TOK_EOF ||
             (tmp = longtok3(p, '!', '=', '=')) != TOK_EOF ||
             (tmp = longtok(p, "&", "&=")) != TOK_EOF ||
             (tmp = longtok(p, "|", "|=")) != TOK_EOF ||
             (tmp = longtok(p, "<", "<=")) != TOK_EOF ||
             (tmp = longtok(p, ">", ">=")) != TOK_EOF ||
             (tmp = longtok(p, "-", "-=")) != TOK_EOF ||
             (tmp = longtok(p, "+", "+=")) != TOK_EOF) {
    tok = tmp;
  } else if ((tmp = longtok(p, "^~+-%/*<>=!|&", "=")) != TOK_EOF) {
    tok = tmp;
  }
  if (p->pos < p->end && p->pos[0] != '\0') p->pos++;
  p->prev_tok = p->tok.tok;
  p->tok.tok = tok;
  // LOG((DBGPREFIX "%s: tok %d [%c] [%.*s]\n", __func__, tok, tok,
  //     (int) (p->end - p->pos), p->pos));
  return p->tok.tok;
}

////////////////////////////////// PARSER /////////////////////////////////

static val_t parse_statement_list(struct parser *p, tok_t endtok);
static val_t parse_expr(struct parser *p);
static val_t parse_statement(struct parser *p);

#define EXPECT(p, t)                                               \
  do {                                                             \
    if ((p)->tok.tok != (t))                                       \
      return vm_err((p)->vm, "%s: expecting '%c'", __func__, (t)); \
  } while (0)

static struct parser mk_parser(struct vm *vm, const char *buf, int len) {
  struct parser p;
  memset(&p, 0, sizeof(p));
  p.line_no = 1;
  p.buf = p.pos = buf;
  p.end = buf + len;
  p.vm = vm;
  return p;
}

// clang-format off
static tok_t s_assign_ops[] = {
  '=', DT('+', '='), DT('-', '='),  DT('*', '='), DT('/', '='), DT('%', '='),
  TT('<', '<', '='), TT('>', '>', '='), QT('>', '>', '>', '='), DT('&', '='),
  DT('^', '='), DT('|', '='), TOK_EOF
};
// clang-format on
static tok_t s_postfix_ops[] = {DT('+', '+'), DT('-', '-'), TOK_EOF};
static tok_t s_unary_ops[] = {'!',        '~', DT('+', '+'), DT('-', '-'),
                              TOK_TYPEOF, '-', '+',          TOK_EOF};
static tok_t s_equality_ops[] = {DT('=', '+'), DT('!', '='), TT('=', '=', '='),
                                 TT('=', '=', '='), TOK_EOF};
static tok_t s_cmp_ops[] = {DT('<', '='), '<', '>', DT('>', '='), TOK_EOF};

static tok_t findtok(const tok_t *toks, tok_t tok) {
  int i = 0;
  while (tok != toks[i] && toks[i] != TOK_EOF) i++;
  return toks[i];
}

static float do_arith_op(float f1, float f2, val_t op) {
  // clang-format off
  switch (op) {
    case '+': return f1 + f2;
    case '-': return f1 - f2;
    case '*': return f1 * f2;
    case '/': return f1 / f2;
    case '%': return (float) ((long) f1 % (long) f2);
    case '^': return (float) ((val_t) f1 ^ (val_t) f2);
    case '|': return (float) ((val_t) f1 | (val_t) f2);
    case '&': return (float) ((val_t) f1 & (val_t) f2);
    case DT('>','>'): return (float) ((long) f1 >> (long) f2);
    case DT('<','<'): return (float) ((long) f1 << (long) f2);
    case TT('>','>', '>'): return (float) ((val_t) f1 >> (val_t) f2);
  }
  // clang-format on
  return 0;
}

static val_t do_assign_op(struct vm *vm, tok_t op) {
  val_t *t = vm_top(vm);
  struct prop *prop = &vm->props[(ind_t) tof(t[-1])];
  if (mjs_type(prop->val) != MJS_TYPE_NUMBER ||
      mjs_type(t[0]) != MJS_TYPE_NUMBER)
    return vm_err(vm, "please no");
  t[-1] = prop->val = tov(do_arith_op(tof(prop->val), tof(t[0]), op));
  vm_drop(vm);
  return prop->val;
}

static val_t do_op(struct parser *p, int op) {
  val_t *top = vm_top(p->vm), a = top[-1], b = top[0];
  if (p->noexec) return MJS_TRUE;
  LOG((DBGPREFIX "%s: sp %d op %c %d\n", __func__, p->vm->sp, op, op));
  LOG((DBGPREFIX "    top-1 %s\n", tostr(p->vm, b)));
  LOG((DBGPREFIX "    top-2 %s\n", tostr(p->vm, a)));
  switch (op) {
    case '+':
      if (mjs_type(a) == MJS_TYPE_STRING && mjs_type(b) == MJS_TYPE_STRING) {
        val_t v = mjs_concat(p->vm, a, b);
        if (v == MJS_ERROR) return v;
        top[-1] = v;
        vm_drop(p->vm);
        break;
      }
    // clang-format off
    case '-': case '*': case '/': case '%': case '^': case '&': case '|':
    case DT('>', '>'): case DT('<', '<'): case TT('>', '>', '>'):
      // clang-format on
      if (mjs_type(a) == MJS_TYPE_NUMBER && mjs_type(b) == MJS_TYPE_NUMBER) {
        top[-1] = tov(do_arith_op(tof(a), tof(b), op));
        vm_drop(p->vm);
      } else {
        return vm_err(p->vm, "apples to apples please");
      }
      break;
    /* clang-format off */
    case DT('-', '='):      return do_assign_op(p->vm, '-');
    case DT('+', '='):      return do_assign_op(p->vm, '+');
    case DT('*', '='):      return do_assign_op(p->vm, '*');
    case DT('/', '='):      return do_assign_op(p->vm, '/');
    case DT('%', '='):      return do_assign_op(p->vm, '%');
    case DT('&', '='):      return do_assign_op(p->vm, '&');
    case DT('|', '='):      return do_assign_op(p->vm, '|');
    case DT('^', '='):      return do_assign_op(p->vm, '^');
    case TT('<', '<', '='): return do_assign_op(p->vm, DT('<', '<'));
    case TT('>', '>', '='): return do_assign_op(p->vm, DT('>', '>'));
    case QT('>', '>', '>', '='):  return do_assign_op(p->vm, TT('>', '>', '>'));
    case ',': break;
    /* clang-format on */
    case TOK_POSTFIX_MINUS:
    case TOK_POSTFIX_PLUS: {
      struct prop *prop = &p->vm->props[(ind_t) tof(b)];
      if (mjs_type(prop->val) != MJS_TYPE_NUMBER)
        return vm_err(p->vm, "please no");
      top[0] = prop->val;
      prop->val = tov(tof(prop->val) + ((op == TOK_POSTFIX_PLUS) ? 1 : -1));
      break;
    }
    case '!':
      top[0] = is_true(p->vm, top[0]) ? MJS_FALSE : MJS_TRUE;
      break;
    case '~':
      if (mjs_type(top[0]) != MJS_TYPE_NUMBER) return vm_err(p->vm, "noo");
      top[0] = tov((float) (~(long) tof(top[0])));
      break;
    case TOK_UNARY_PLUS:
      break;
    case TOK_UNARY_MINUS:
      top[0] = tov(-tof(top[0]));
      break;
      // static tok_t s_unary_ops[] = {'!',        '~', DT('+', '+'), DT('-',
      // '-'),
      //                              TOK_TYPEOF, '-', '+',          TOK_EOF};
    case '=': {
      val_t obj = p->vm->call_stack[p->vm->csp - 1];
      val_t res = mjs_set(p->vm, obj, a, b);
      top[0] = a;
      top[-1] = b;
      if (res == MJS_ERROR) return res;
      return vm_drop(p->vm);
    }
    default:
      return vm_err(p->vm, "Unknown op: %c (%d)", op, op);
  }
  return MJS_TRUE;
}

typedef val_t bpf_t(struct parser *p, int prev_op);

static val_t parse_ltr_binop(struct parser *p, bpf_t f1, bpf_t f2,
                             const tok_t *ops, tok_t prev_op) {
  val_t res = MJS_TRUE;
  TRY(f1(p, TOK_EOF));
  if (prev_op != TOK_EOF) TRY(do_op(p, prev_op));
  if (findtok(ops, p->tok.tok) != TOK_EOF) {
    int op = p->tok.tok;
    pnext(p);
    TRY(f2(p, op));
  }
  return res;
}

static val_t parse_rtl_binop(struct parser *p, bpf_t f1, bpf_t f2,
                             const tok_t *ops, tok_t prev_op) {
  val_t res = MJS_TRUE;
  (void) prev_op;
  TRY(f1(p, TOK_EOF));
  if (findtok(ops, p->tok.tok) != TOK_EOF) {
    int op = p->tok.tok;
    pnext(p);
    TRY(f2(p, TOK_EOF));
    TRY(do_op(p, op));
  }
  return res;
}

static tok_t lookahead(struct parser *p) {
  struct parser tmp = *p;
  tok_t tok = pnext(p);
  *p = tmp;
  return tok;
}

static val_t parse_block(struct parser *p, int mkscope) {
  val_t res = MJS_TRUE;
  if (mkscope && !p->noexec) TRY(create_scope(p->vm));
  TRY(parse_statement_list(p, '}'));
  EXPECT(p, '}');
  if (mkscope && !p->noexec) TRY(delete_scope(p->vm));
  return res;
}

static val_t parse_function(struct parser *p) {
  val_t res = MJS_TRUE;
  int name_provided = 0;
  struct tok tmp = p->tok;
  LOG((DBGPREFIX "%s: START: [%d]\n", __func__, p->vm->sp));
  p->noexec++;
  pnext(p);
  if (p->tok.tok == TOK_IDENT) {  // Function name provided: function ABC()...
    name_provided = 1;
    pnext(p);
  }
  EXPECT(p, '(');
  pnext(p);
  // Emit names of function arguments
  while (p->tok.tok != ')') {
    EXPECT(p, TOK_IDENT);
    if (lookahead(p) == ',') pnext(p);
    pnext(p);
  }
  EXPECT(p, ')');
  pnext(p);
  TRY(parse_block(p, 0));
  if (name_provided) TRY(do_op(p, '='));
  {
    val_t f = mk_func(p->vm, tmp.ptr, p->tok.ptr - tmp.ptr + 1);
    TRY(f);
    res = vm_push(p->vm, f);
  }
  p->noexec--;
  LOG((DBGPREFIX "%s: STOP: [%d]\n", __func__, p->vm->sp));
  return res;
}

static val_t parse_object_literal(struct parser *p) {
  val_t obj = MJS_UNDEFINED, key, val, res = MJS_TRUE;
  pnext(p);
  if (!p->noexec) {
    TRY(mk_obj(p->vm));
    obj = res;
    TRY(vm_push(p->vm, obj));
  }
  while (p->tok.tok != '}') {
    if (p->tok.tok != TOK_IDENT && p->tok.tok != TOK_STR)
      return vm_err(p->vm, "error parsing obj key");
    key = mk_str(p->vm, p->tok.ptr, p->tok.len);
    TRY(key);
    pnext(p);
    EXPECT(p, ':');
    pnext(p);
    TRY(parse_expr(p));
    if (!p->noexec) {
      val = *vm_top(p->vm);
      TRY(mjs_set(p->vm, obj, key, val));
      vm_drop(p->vm);
    }
    if (p->tok.tok == ',') {
      pnext(p);
    } else if (p->tok.tok != '}') {
      return vm_err(p->vm, "parsing obj: expecting '}'");
    }
  }
  // printf("mko %s\n", tostr(p->vm, obj));
  return res;
}

static val_t parse_literal(struct parser *p, tok_t prev_op) {
  val_t res = MJS_TRUE;
  (void) prev_op;
  switch (p->tok.tok) {
    case TOK_NUM:
      if (!p->noexec) TRY(vm_push(p->vm, tov(p->tok.num_value)));
      break;
    case TOK_STR:
      if (!p->noexec) {
        val_t v = mk_str(p->vm, p->tok.ptr, p->tok.len);
        TRY(v);
        TRY(vm_push(p->vm, v));
      }
      break;
    case '{':
      res = parse_object_literal(p);
      break;
    case TOK_IDENT:
      // LOG((DBGPREFIX "%s: IDENT: [%d]\n", __func__, prev_op));
      if (!p->noexec) {
        tok_t prev_tok = p->prev_tok;
        tok_t next_tok = lookahead(p);
        if (!findtok(s_assign_ops, next_tok) &&
            !findtok(s_postfix_ops, next_tok) &&
            !findtok(s_postfix_ops, prev_tok)) {
          // Get value
          res = lookup_and_push(p->vm, p->tok.ptr, p->tok.len);
        } else {
          // Assign
          val_t *v = lookup(p->vm, p->tok.ptr, p->tok.len);
          LOG((DBGPREFIX "%s: AS: [%.*s]\n", __func__, p->tok.len, p->tok.ptr));
          if (v == NULL) {
            return vm_err(p->vm, "doh");
          } else {
            // Push the index of a property that holds this key
            size_t off = offsetof(struct prop, val);
            struct prop *prop = (struct prop *) ((char *) v - off);
            ind_t ind = (ind_t)(prop - p->vm->props);
            LOG((DBGPREFIX "   ind %d\n", ind));
            TRY(vm_push(p->vm, tov(ind)));
          }
        }
      }
      break;
    case TOK_FUNCTION:
      res = parse_function(p);
      break;
    case TOK_TRUE:
      res = vm_push(p->vm, MJS_TRUE);
      break;
    case TOK_FALSE:
      res = vm_push(p->vm, MJS_FALSE);
      break;
    case TOK_NULL:
      res = vm_push(p->vm, MJS_NULL);
      break;
    case TOK_UNDEFINED:
      res = vm_push(p->vm, MJS_UNDEFINED);
      break;
    case '(':
      pnext(p);
      res = parse_expr(p);
      EXPECT(p, ')');
      break;
    default:
      return vm_err(p->vm, "Bad literal: [%.*s]", p->tok.len, p->tok.ptr);
      break;
  }
  pnext(p);
  return res;
}

static void setarg(struct parser *p, val_t scope, val_t val) {
  val_t key = mk_str(p->vm, p->tok.ptr, p->tok.len);
  if (mjs_type(key) == MJS_TYPE_STRING) mjs_set(p->vm, scope, key, val);
  // printf("  setarg: key %s\n", tostr(p->vm, key));
  // printf("  setarg: val %s\n", tostr(p->vm, val));
  // printf("  setarg scope: %s\n", tostr(p->vm, scope));
  if (lookahead(p) == ',') pnext(p);
  pnext(p);
}

static val_t call_js_function(struct parser *p, val_t f) {
  val_t res = MJS_TRUE;
  ind_t saved_scp = p->vm->csp;
  val_t scope;  // Function to call

  // Create parser for the function code
  len_t code_len;
  char *code = mjs_to_str(p->vm, f, &code_len);
  struct parser p2 = mk_parser(p->vm, code, code_len);

  // Create scope
  TRY(create_scope(p->vm));
  scope = p->vm->call_stack[p->vm->csp - 1];
  LOG((DBGPREFIX "%s: %d [%.*s]\n", __func__, mjs_type(scope), code_len, code));

  // Skip `function(` in the function definition
  pnext(&p2);
  pnext(&p2);
  pnext(&p2);  // Now p2.tok points either to the first argument, or to the ')'

  // Parse parameters, populate the scope as local variables
  while (p->tok.tok != ')') {
    // Evaluate next argument - pushed to the data_stack
    TRY(parse_expr(p));
    if (p->tok.tok == ',') pnext(p);
    // Check whether we have a defined name for this argument
    if (p2.tok.tok == TOK_IDENT) setarg(&p2, scope, *vm_top(p->vm));
    vm_drop(p->vm);  // Drop argument value from the data_stack
    LOG((DBGPREFIX "%s: P sp %d\n", __func__, p->vm->sp));
  }
  // printf(" local scope: %s\n", tostr(p->vm, scope));
  while (p2.tok.tok == TOK_IDENT) setarg(&p2, scope, MJS_UNDEFINED);
  while (p2.tok.tok != '{') pnext(&p2);  // Skip to the function body
  res = parse_block(&p2, 0);             // Execute function body
  LOG((DBGPREFIX "%s: R sp %d\n", __func__, p->vm->sp));
  while (p->vm->csp > saved_scp) delete_scope(p->vm);  // Restore current scope
  return res;
}

#define FFI_MAX_ARGS_CNT 6
typedef intptr_t ffi_word_t;

enum ffi_ctype {
  FFI_CTYPE_WORD,
  FFI_CTYPE_BOOL,
  FFI_CTYPE_FLOAT,
  FFI_CTYPE_DOUBLE,
};

union ffi_val {
  ffi_word_t w;
  // int64_t i;
  unsigned long i;
  double d;
  float f;
};

struct ffi_arg {
  enum ffi_ctype ctype;
  union ffi_val v;
};

#define IS_W(arg) ((arg).ctype == FFI_CTYPE_WORD)
#define IS_D(arg) ((arg).ctype == FFI_CTYPE_DOUBLE)
#define IS_F(arg) ((arg).ctype == FFI_CTYPE_FLOAT)

#define W(arg) ((ffi_word_t)(arg).v.i)
#define D(arg) ((arg).v.d)
#define F(arg) ((arg).v.f)

static void ffi_set_word(struct ffi_arg *arg, ffi_word_t v) {
  arg->ctype = FFI_CTYPE_WORD;
  arg->v.i = v;
}

static void ffi_set_bool(struct ffi_arg *arg, bool v) {
  arg->ctype = FFI_CTYPE_BOOL;
  arg->v.i = v;
}

static void ffi_set_ptr(struct ffi_arg *arg, void *v) {
  ffi_set_word(arg, (ffi_word_t) v);
}

static void ffi_set_double(struct ffi_arg *arg, double v) {
  arg->ctype = FFI_CTYPE_DOUBLE;
  arg->v.d = v;
}

static void ffi_set_float(struct ffi_arg *arg, float v) {
  arg->ctype = FFI_CTYPE_FLOAT;
  arg->v.f = v;
}

/*
 * The ARM ABI uses only 4 32-bit registers for paramter passing.
 * Xtensa call0 calling-convention (as used by Espressif) has 6.
 *
 * Focusing only on implementing FFI with registers means we can simplify a lot.
 *
 * ARM has some quasi-alignment rules when mixing double and integers as
 * arguments. Only:
 *   a) double, int32_t, int32_t
 *   b) int32_t, double
 * would fit in 4 registers. (the same goes for uint64_t).
 *
 * In order to simplify further, when a double-width argument is present, we
 * allow only two arguments.
 */

/*
 * We need to support x86_64 in order to support local tests.
 * x86_64 has more and wider registers, but unlike the two main
 * embedded platforms we target it has a separate register file for
 * integer values and for floating point values (both for passing args and
 * return values). E.g. if a double value is passed as a second argument
 * it gets passed in the first available floating point register.
 *
 * I.e, the compiler generates exactly the same code for:
 *
 * void foo(int a, double b) {...}
 *
 * and
 *
 * void foo(double b, int a) {...}
 *
 *
 */

typedef ffi_word_t (*w4w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t);
typedef ffi_word_t (*w5w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                            ffi_word_t);
typedef ffi_word_t (*w6w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                            ffi_word_t, ffi_word_t);

typedef ffi_word_t (*wdw_t)(double, ffi_word_t);
typedef ffi_word_t (*wwd_t)(ffi_word_t, double);
typedef ffi_word_t (*wdd_t)(double, double);

typedef ffi_word_t (*wwwd_t)(ffi_word_t, ffi_word_t, double);
typedef ffi_word_t (*wwdw_t)(ffi_word_t, double, ffi_word_t);
typedef ffi_word_t (*wwdd_t)(ffi_word_t, double, double);
typedef ffi_word_t (*wdww_t)(double, ffi_word_t, ffi_word_t);
typedef ffi_word_t (*wdwd_t)(double, ffi_word_t, double);
typedef ffi_word_t (*wddw_t)(double, double, ffi_word_t);
typedef ffi_word_t (*wddd_t)(double, double, double);

typedef ffi_word_t (*wfw_t)(float, ffi_word_t);
typedef ffi_word_t (*wwf_t)(ffi_word_t, float);
typedef ffi_word_t (*wff_t)(float, float);

typedef ffi_word_t (*wwwf_t)(ffi_word_t, ffi_word_t, float);
typedef ffi_word_t (*wwfw_t)(ffi_word_t, float, ffi_word_t);
typedef ffi_word_t (*wwff_t)(ffi_word_t, float, float);
typedef ffi_word_t (*wfww_t)(float, ffi_word_t, ffi_word_t);
typedef ffi_word_t (*wfwf_t)(float, ffi_word_t, float);
typedef ffi_word_t (*wffw_t)(float, float, ffi_word_t);
typedef ffi_word_t (*wfff_t)(float, float, float);

typedef bool (*b4w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t);
typedef bool (*b5w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                      ffi_word_t);
typedef bool (*b6w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                      ffi_word_t, ffi_word_t);
typedef bool (*bdw_t)(double, ffi_word_t);
typedef bool (*bwd_t)(ffi_word_t, double);
typedef bool (*bdd_t)(double, double);

typedef bool (*bwwd_t)(ffi_word_t, ffi_word_t, double);
typedef bool (*bwdw_t)(ffi_word_t, double, ffi_word_t);
typedef bool (*bwdd_t)(ffi_word_t, double, double);
typedef bool (*bdww_t)(double, ffi_word_t, ffi_word_t);
typedef bool (*bdwd_t)(double, ffi_word_t, double);
typedef bool (*bddw_t)(double, double, ffi_word_t);
typedef bool (*bddd_t)(double, double, double);

typedef bool (*bfw_t)(float, ffi_word_t);
typedef bool (*bwf_t)(ffi_word_t, float);
typedef bool (*bff_t)(float, float);

typedef bool (*bwwf_t)(ffi_word_t, ffi_word_t, float);
typedef bool (*bwfw_t)(ffi_word_t, float, ffi_word_t);
typedef bool (*bwff_t)(ffi_word_t, float, float);
typedef bool (*bfww_t)(float, ffi_word_t, ffi_word_t);
typedef bool (*bfwf_t)(float, ffi_word_t, float);
typedef bool (*bffw_t)(float, float, ffi_word_t);
typedef bool (*bfff_t)(float, float, float);

typedef double (*d4w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t);
typedef double (*d5w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                        ffi_word_t);
typedef double (*d6w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                        ffi_word_t, ffi_word_t);
typedef double (*ddw_t)(double, ffi_word_t);
typedef double (*dwd_t)(ffi_word_t, double);
typedef double (*ddd_t)(double, double);

typedef double (*dwwd_t)(ffi_word_t, ffi_word_t, double);
typedef double (*dwdw_t)(ffi_word_t, double, ffi_word_t);
typedef double (*dwdd_t)(ffi_word_t, double, double);
typedef double (*ddww_t)(double, ffi_word_t, ffi_word_t);
typedef double (*ddwd_t)(double, ffi_word_t, double);
typedef double (*dddw_t)(double, double, ffi_word_t);
typedef double (*dddd_t)(double, double, double);

typedef float (*f4w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t);
typedef float (*f5w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                       ffi_word_t);
typedef float (*f6w_t)(ffi_word_t, ffi_word_t, ffi_word_t, ffi_word_t,
                       ffi_word_t, ffi_word_t);
typedef float (*ffw_t)(float, ffi_word_t);
typedef float (*fwf_t)(ffi_word_t, float);
typedef float (*fff_t)(float, float);

typedef float (*fwwf_t)(ffi_word_t, ffi_word_t, float);
typedef float (*fwfw_t)(ffi_word_t, float, ffi_word_t);
typedef float (*fwff_t)(ffi_word_t, float, float);
typedef float (*ffww_t)(float, ffi_word_t, ffi_word_t);
typedef float (*ffwf_t)(float, ffi_word_t, float);
typedef float (*fffw_t)(float, float, ffi_word_t);
typedef float (*ffff_t)(float, float, float);

static int ffi_call(cfn_t func, int nargs, struct ffi_arg *res,
                    struct ffi_arg *args) {
  int i, doubles = 0, floats = 0;

  if (nargs > 6) return -1;
  for (i = 0; i < nargs; i++) {
    doubles += (IS_D(args[i]));
    floats += (IS_F(args[i]));
  }

  // Doubles and floats are not supported together atm
  if (doubles > 0 && floats > 0) return -1;

  switch (res->ctype) {
    case FFI_CTYPE_WORD: {
      ffi_word_t r;
      if (doubles == 0) {
        if (floats == 0) {
          // No double and no float args: we currently support up to 6
          // word-sized arguments
          if (nargs <= 4) {
            w4w_t f = (w4w_t) func;
            r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]));
          } else if (nargs == 5) {
            w5w_t f = (w5w_t) func;
            r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]));
          } else if (nargs == 6) {
            w6w_t f = (w6w_t) func;
            r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]),
                  W(args[5]));
          } else {
            abort();
          }
        } else {
          // There are some floats
          switch (nargs) {
            case 0:
            case 1:
            case 2:
              if (IS_F(args[0]) && IS_F(args[1])) {
                wff_t f = (wff_t) func;
                r = f(F(args[0]), F(args[1]));
              } else if (IS_F(args[0])) {
                wfw_t f = (wfw_t) func;
                r = f(F(args[0]), W(args[1]));
              } else {
                wwf_t f = (wwf_t) func;
                r = f(W(args[0]), F(args[1]));
              }
              break;

            case 3:
              if (IS_W(args[0]) && IS_W(args[1]) && IS_F(args[2])) {
                wwwf_t f = (wwwf_t) func;
                r = f(W(args[0]), W(args[1]), F(args[2]));
              } else if (IS_W(args[0]) && IS_F(args[1]) && IS_W(args[2])) {
                wwfw_t f = (wwfw_t) func;
                r = f(W(args[0]), F(args[1]), W(args[2]));
              } else if (IS_W(args[0]) && IS_F(args[1]) && IS_F(args[2])) {
                wwff_t f = (wwff_t) func;
                r = f(W(args[0]), F(args[1]), F(args[2]));
              } else if (IS_F(args[0]) && IS_W(args[1]) && IS_W(args[2])) {
                wfww_t f = (wfww_t) func;
                r = f(F(args[0]), W(args[1]), W(args[2]));
              } else if (IS_F(args[0]) && IS_W(args[1]) && IS_F(args[2])) {
                wfwf_t f = (wfwf_t) func;
                r = f(F(args[0]), W(args[1]), F(args[2]));
              } else if (IS_F(args[0]) && IS_F(args[1]) && IS_W(args[2])) {
                wffw_t f = (wffw_t) func;
                r = f(F(args[0]), F(args[1]), W(args[2]));
              } else if (IS_F(args[0]) && IS_F(args[1]) && IS_F(args[2])) {
                wfff_t f = (wfff_t) func;
                r = f(F(args[0]), F(args[1]), F(args[2]));
              } else {
                // The above checks should be exhaustive
                abort();
              }
              break;
            default:
              return -1;
          }
        }
      } else {
        // There are some doubles
        switch (nargs) {
          case 0:
          case 1:
          case 2:
            if (IS_D(args[0]) && IS_D(args[1])) {
              wdd_t f = (wdd_t) func;
              r = f(D(args[0]), D(args[1]));
            } else if (IS_D(args[0])) {
              wdw_t f = (wdw_t) func;
              r = f(D(args[0]), W(args[1]));
            } else {
              wwd_t f = (wwd_t) func;
              r = f(W(args[0]), D(args[1]));
            }
            break;

          case 3:
            if (IS_W(args[0]) && IS_W(args[1]) && IS_D(args[2])) {
              wwwd_t f = (wwwd_t) func;
              r = f(W(args[0]), W(args[1]), D(args[2]));
            } else if (IS_W(args[0]) && IS_D(args[1]) && IS_W(args[2])) {
              wwdw_t f = (wwdw_t) func;
              r = f(W(args[0]), D(args[1]), W(args[2]));
            } else if (IS_W(args[0]) && IS_D(args[1]) && IS_D(args[2])) {
              wwdd_t f = (wwdd_t) func;
              r = f(W(args[0]), D(args[1]), D(args[2]));
            } else if (IS_D(args[0]) && IS_W(args[1]) && IS_W(args[2])) {
              wdww_t f = (wdww_t) func;
              r = f(D(args[0]), W(args[1]), W(args[2]));
            } else if (IS_D(args[0]) && IS_W(args[1]) && IS_D(args[2])) {
              wdwd_t f = (wdwd_t) func;
              r = f(D(args[0]), W(args[1]), D(args[2]));
            } else if (IS_D(args[0]) && IS_D(args[1]) && IS_W(args[2])) {
              wddw_t f = (wddw_t) func;
              r = f(D(args[0]), D(args[1]), W(args[2]));
            } else if (IS_D(args[0]) && IS_D(args[1]) && IS_D(args[2])) {
              wddd_t f = (wddd_t) func;
              r = f(D(args[0]), D(args[1]), D(args[2]));
            } else {
              // The above checks should be exhaustive
              abort();
            }
            break;
          default:
            return -1;
        }
      }
      res->v.i = (ffi_word_t) r;
    } break;
    case FFI_CTYPE_BOOL: {
      ffi_word_t r;
      if (doubles == 0) {
        if (floats == 0) {
          // No double and no float args: we currently support up to 6
          // word-sized arguments
          if (nargs <= 4) {
            b4w_t f = (b4w_t) func;
            r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]));
          } else if (nargs == 5) {
            b5w_t f = (b5w_t) func;
            r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]));
          } else if (nargs == 6) {
            b6w_t f = (b6w_t) func;
            r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]),
                  W(args[5]));
          } else {
            abort();
          }
        } else {
          // There are some floats
          switch (nargs) {
            case 0:
            case 1:
            case 2:
              if (IS_F(args[0]) && IS_F(args[1])) {
                bff_t f = (bff_t) func;
                r = f(F(args[0]), F(args[1]));
              } else if (IS_F(args[0])) {
                bfw_t f = (bfw_t) func;
                r = f(F(args[0]), W(args[1]));
              } else {
                bwf_t f = (bwf_t) func;
                r = f(W(args[0]), F(args[1]));
              }
              break;

            case 3:
              if (IS_W(args[0]) && IS_W(args[1]) && IS_F(args[2])) {
                bwwf_t f = (bwwf_t) func;
                r = f(W(args[0]), W(args[1]), F(args[2]));
              } else if (IS_W(args[0]) && IS_F(args[1]) && IS_W(args[2])) {
                bwfw_t f = (bwfw_t) func;
                r = f(W(args[0]), F(args[1]), W(args[2]));
              } else if (IS_W(args[0]) && IS_F(args[1]) && IS_F(args[2])) {
                bwff_t f = (bwff_t) func;
                r = f(W(args[0]), F(args[1]), F(args[2]));
              } else if (IS_F(args[0]) && IS_W(args[1]) && IS_W(args[2])) {
                bfww_t f = (bfww_t) func;
                r = f(F(args[0]), W(args[1]), W(args[2]));
              } else if (IS_F(args[0]) && IS_W(args[1]) && IS_F(args[2])) {
                bfwf_t f = (bfwf_t) func;
                r = f(F(args[0]), W(args[1]), F(args[2]));
              } else if (IS_F(args[0]) && IS_F(args[1]) && IS_W(args[2])) {
                bffw_t f = (bffw_t) func;
                r = f(F(args[0]), F(args[1]), W(args[2]));
              } else if (IS_F(args[0]) && IS_F(args[1]) && IS_F(args[2])) {
                bfff_t f = (bfff_t) func;
                r = f(F(args[0]), F(args[1]), F(args[2]));
              } else {
                // The above checks should be exhaustive
                abort();
              }
              break;
            default:
              return -1;
          }
        }
      } else {
        // There are some doubles
        switch (nargs) {
          case 0:
          case 1:
          case 2:
            if (IS_D(args[0]) && IS_D(args[1])) {
              bdd_t f = (bdd_t) func;
              r = f(D(args[0]), D(args[1]));
            } else if (IS_D(args[0])) {
              bdw_t f = (bdw_t) func;
              r = f(D(args[0]), W(args[1]));
            } else {
              bwd_t f = (bwd_t) func;
              r = f(W(args[0]), D(args[1]));
            }
            break;

          case 3:
            if (IS_W(args[0]) && IS_W(args[1]) && IS_D(args[2])) {
              bwwd_t f = (bwwd_t) func;
              r = f(W(args[0]), W(args[1]), D(args[2]));
            } else if (IS_W(args[0]) && IS_D(args[1]) && IS_W(args[2])) {
              bwdw_t f = (bwdw_t) func;
              r = f(W(args[0]), D(args[1]), W(args[2]));
            } else if (IS_W(args[0]) && IS_D(args[1]) && IS_D(args[2])) {
              bwdd_t f = (bwdd_t) func;
              r = f(W(args[0]), D(args[1]), D(args[2]));
            } else if (IS_D(args[0]) && IS_W(args[1]) && IS_W(args[2])) {
              bdww_t f = (bdww_t) func;
              r = f(D(args[0]), W(args[1]), W(args[2]));
            } else if (IS_D(args[0]) && IS_W(args[1]) && IS_D(args[2])) {
              bdwd_t f = (bdwd_t) func;
              r = f(D(args[0]), W(args[1]), D(args[2]));
            } else if (IS_D(args[0]) && IS_D(args[1]) && IS_W(args[2])) {
              bddw_t f = (bddw_t) func;
              r = f(D(args[0]), D(args[1]), W(args[2]));
            } else if (IS_D(args[0]) && IS_D(args[1]) && IS_D(args[2])) {
              bddd_t f = (bddd_t) func;
              r = f(D(args[0]), D(args[1]), D(args[2]));
            } else {
              // The above checks should be exhaustive
              abort();
            }
            break;
          default:
            return -1;
        }
      }
      res->v.i = (ffi_word_t) r;
    } break;
    case FFI_CTYPE_DOUBLE: {
      double r;
      if (doubles == 0) {
        /* No double args: we currently support up to 6 word-sized arguments
         */
        if (nargs <= 4) {
          d4w_t f = (d4w_t) func;
          r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]));
        } else if (nargs == 5) {
          d5w_t f = (d5w_t) func;
          r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]));
        } else if (nargs == 6) {
          d6w_t f = (d6w_t) func;
          r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]),
                W(args[5]));
        } else {
          abort();
        }
      } else {
        switch (nargs) {
          case 0:
          case 1:
          case 2:
            if (IS_D(args[0]) && IS_D(args[1])) {
              ddd_t f = (ddd_t) func;
              r = f(D(args[0]), D(args[1]));
            } else if (IS_D(args[0])) {
              ddw_t f = (ddw_t) func;
              r = f(D(args[0]), W(args[1]));
            } else {
              dwd_t f = (dwd_t) func;
              r = f(W(args[0]), D(args[1]));
            }
            break;

          case 3:
            if (IS_W(args[0]) && IS_W(args[1]) && IS_D(args[2])) {
              dwwd_t f = (dwwd_t) func;
              r = f(W(args[0]), W(args[1]), D(args[2]));
            } else if (IS_W(args[0]) && IS_D(args[1]) && IS_W(args[2])) {
              dwdw_t f = (dwdw_t) func;
              r = f(W(args[0]), D(args[1]), W(args[2]));
            } else if (IS_W(args[0]) && IS_D(args[1]) && IS_D(args[2])) {
              dwdd_t f = (dwdd_t) func;
              r = f(W(args[0]), D(args[1]), D(args[2]));
            } else if (IS_D(args[0]) && IS_W(args[1]) && IS_W(args[2])) {
              ddww_t f = (ddww_t) func;
              r = f(D(args[0]), W(args[1]), W(args[2]));
            } else if (IS_D(args[0]) && IS_W(args[1]) && IS_D(args[2])) {
              ddwd_t f = (ddwd_t) func;
              r = f(D(args[0]), W(args[1]), D(args[2]));
            } else if (IS_D(args[0]) && IS_D(args[1]) && IS_W(args[2])) {
              dddw_t f = (dddw_t) func;
              r = f(D(args[0]), D(args[1]), W(args[2]));
            } else if (IS_D(args[0]) && IS_D(args[1]) && IS_D(args[2])) {
              dddd_t f = (dddd_t) func;
              r = f(D(args[0]), D(args[1]), D(args[2]));
            } else {
              // The above checks should be exhaustive
              abort();
            }
            break;
          default:
            return -1;
        }
      }
      res->v.d = r;
    } break;
    case FFI_CTYPE_FLOAT: {
      double r;
      if (floats == 0) {
        // No float args: we currently support up to 6 word-sized arguments
        if (nargs <= 4) {
          f4w_t f = (f4w_t) func;
          r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]));
        } else if (nargs == 5) {
          f5w_t f = (f5w_t) func;
          r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]));
        } else if (nargs == 6) {
          f6w_t f = (f6w_t) func;
          r = f(W(args[0]), W(args[1]), W(args[2]), W(args[3]), W(args[4]),
                W(args[5]));
        } else {
          abort();
        }
      } else {
        // There are some floats
        switch (nargs) {
          case 0:
          case 1:
          case 2:
            if (IS_F(args[0]) && IS_F(args[1])) {
              fff_t f = (fff_t) func;
              r = f(F(args[0]), F(args[1]));
            } else if (IS_F(args[0])) {
              ffw_t f = (ffw_t) func;
              r = f(F(args[0]), W(args[1]));
            } else {
              fwf_t f = (fwf_t) func;
              r = f(W(args[0]), F(args[1]));
            }
            break;

          case 3:
            if (IS_W(args[0]) && IS_W(args[1]) && IS_F(args[2])) {
              fwwf_t f = (fwwf_t) func;
              r = f(W(args[0]), W(args[1]), F(args[2]));
            } else if (IS_W(args[0]) && IS_F(args[1]) && IS_W(args[2])) {
              fwfw_t f = (fwfw_t) func;
              r = f(W(args[0]), F(args[1]), W(args[2]));
            } else if (IS_W(args[0]) && IS_F(args[1]) && IS_F(args[2])) {
              fwff_t f = (fwff_t) func;
              r = f(W(args[0]), F(args[1]), F(args[2]));
            } else if (IS_F(args[0]) && IS_W(args[1]) && IS_W(args[2])) {
              ffww_t f = (ffww_t) func;
              r = f(F(args[0]), W(args[1]), W(args[2]));
            } else if (IS_F(args[0]) && IS_W(args[1]) && IS_F(args[2])) {
              ffwf_t f = (ffwf_t) func;
              r = f(F(args[0]), W(args[1]), F(args[2]));
            } else if (IS_F(args[0]) && IS_F(args[1]) && IS_W(args[2])) {
              fffw_t f = (fffw_t) func;
              r = f(F(args[0]), F(args[1]), W(args[2]));
            } else if (IS_F(args[0]) && IS_F(args[1]) && IS_F(args[2])) {
              ffff_t f = (ffff_t) func;
              r = f(F(args[0]), F(args[1]), F(args[2]));
            } else {
              // The above checks should be exhaustive
              abort();
            }
            break;
          default:
            return -1;
        }
      }
      res->v.f = (float) r;
    } break;
  }

  return 0;
}

struct fficbparam {
  struct parser *p;
  const char *decl;
  val_t jsfunc;
};

static ffi_word_t fficb(struct fficbparam *cbp, union ffi_val *args) {
  char buf[100];
  int num_args = 0, n = 0;
  const char *s;
  struct parser p2;
  val_t res;
  for (s = cbp->decl + 1; *s != '\0' && *s != ']'; s++) {
    // clang-format off
    if (num_args > 0) n += snprintf(buf + n, sizeof(buf) - n, "%s", ",");
    switch (*s) {
      case 'i': n += snprintf(buf + n, sizeof(buf) - n, "%d", (int) args[num_args].i); break;
      default: n += snprintf(buf + n, sizeof(buf) - n, "null"); break;
    }
    // clang-format on
    num_args++;
  }
  n += snprintf(buf + n, sizeof(buf) - n, "%s", ")");
  // printf("js cb: %p '%s'\n", cbp, buf);
  p2 = mk_parser(cbp->p->vm, buf, n);
  pnext(&p2);
  call_js_function(&p2, cbp->jsfunc);
  res = *vm_top(cbp->p->vm);
  // printf("js cb res: %s\n", tostr(cbp->p->vm, res));
  return (ffi_word_t) tof(res);
}

static void ffiinitcbargs(union ffi_val *args, ffi_word_t w1, ffi_word_t w2,
                          ffi_word_t w3, ffi_word_t w4, ffi_word_t w5,
                          ffi_word_t w6) {
  args[0].i = w1;
  args[1].i = w2;
  args[2].i = w3;
  args[3].i = w4;
  args[4].i = w5;
  args[5].i = w6;
}

static ffi_word_t fficb1(ffi_word_t w1, ffi_word_t w2, ffi_word_t w3,
                         ffi_word_t w4, ffi_word_t w5, ffi_word_t w6) {
  union ffi_val args[FFI_MAX_ARGS_CNT];
  ffiinitcbargs(args, w1, w2, w3, w4, w5, w6);
  return fficb((struct fficbparam *) w1, args);
}

static ffi_word_t fficb2(ffi_word_t w1, ffi_word_t w2, ffi_word_t w3,
                         ffi_word_t w4, ffi_word_t w5, ffi_word_t w6) {
  union ffi_val args[FFI_MAX_ARGS_CNT];
  ffiinitcbargs(args, w1, w2, w3, w4, w5, w6);
  return fficb((struct fficbparam *) w2, args);
}

static ffi_word_t fficb3(ffi_word_t w1, ffi_word_t w2, ffi_word_t w3,
                         ffi_word_t w4, ffi_word_t w5, ffi_word_t w6) {
  union ffi_val args[FFI_MAX_ARGS_CNT];
  ffiinitcbargs(args, w1, w2, w3, w4, w5, w6);
  return fficb((struct fficbparam *) w3, args);
}

static ffi_word_t fficb4(ffi_word_t w1, ffi_word_t w2, ffi_word_t w3,
                         ffi_word_t w4, ffi_word_t w5, ffi_word_t w6) {
  union ffi_val args[FFI_MAX_ARGS_CNT];
  ffiinitcbargs(args, w1, w2, w3, w4, w5, w6);
  return fficb((struct fficbparam *) w4, args);
}

static ffi_word_t fficb5(ffi_word_t w1, ffi_word_t w2, ffi_word_t w3,
                         ffi_word_t w4, ffi_word_t w5, ffi_word_t w6) {
  union ffi_val args[FFI_MAX_ARGS_CNT];
  ffiinitcbargs(args, w1, w2, w3, w4, w5, w6);
  return fficb((struct fficbparam *) w5, args);
}

static ffi_word_t fficb6(ffi_word_t w1, ffi_word_t w2, ffi_word_t w3,
                         ffi_word_t w4, ffi_word_t w5, ffi_word_t w6) {
  union ffi_val args[FFI_MAX_ARGS_CNT];
  ffiinitcbargs(args, w1, w2, w3, w4, w5, w6);
  return fficb((struct fficbparam *) w6, args);
}

static w6w_t setfficb(struct parser *p, val_t jsfunc, struct fficbparam *cbp,
                      const char *decl, int *idx) {
  w6w_t res = 0, cbs[] = {fficb1, fficb2, fficb3, fficb4, fficb5, fficb6, 0};
  int i = 0;
  cbp->p = p;
  cbp->jsfunc = jsfunc;
  cbp->decl = decl + *idx + 1;
  if (decl[*idx] != ']') (*idx)++;  // Skip callback return value type
  while (decl[*idx + 1] != '\0' && decl[*idx] != ']') {
    (*idx)++;
    if (decl[*idx] == 'u') res = cbs[i];
    if (cbs[i] != NULL) i++;
  }
  return res;
}

static val_t call_c_function(struct parser *p, val_t f) {
  struct cfunc *cf = &p->vm->cfuncs[VAL_PAYLOAD(f)];
  val_t res = MJS_UNDEFINED, v = MJS_UNDEFINED, *top = vm_top(p->vm);
  struct ffi_arg args[FFI_MAX_ARGS_CNT + 1];  // First arg - return value
  struct fficbparam cbp;                      // For C callbacks only
  int i, num_passed_args = 0, num_expected_args = 0;

  // Evaluate all JS parameters passed to the C function, push them on stack
  while (p->tok.tok != ')') {
    TRY(parse_expr(p));               // Push to the data_stack
    if (p->tok.tok == ',') pnext(p);  // Skip to the next arg
    num_passed_args++;
  }

  // clang-format off
	// Set type of the return value
  memset(args, 0, sizeof(args));
  memset(&cbp, 0, sizeof(cbp));
	//printf("--> cbp %p\n", &cbp);
	switch (cf->decl[0]) {
		case 'f': args[0].ctype = FFI_CTYPE_FLOAT; break;
		case 'F': args[0].ctype = FFI_CTYPE_DOUBLE; break;
		case 'b': args[0].ctype = FFI_CTYPE_BOOL; break;
		default: args[0].ctype = FFI_CTYPE_WORD; break;
	}
	// Prepare FFI arguments - fetch them from the passed JS arguments
	for (i = 1; cf->decl[i] != '\0'; i++) {  // Start from 1 to skip ret value
		struct ffi_arg *arg = &args[num_expected_args + 1];
		val_t av = top[num_expected_args + 1];
		//printf("--> arg [%c] [%s]\n", cf->decl[i], tostr(p->vm, av));
		switch (cf->decl[i]) {
			case '[': ffi_set_ptr(arg, (void *) setfficb(p, av, &cbp, cf->decl, &i)); break;
			case 'u': ffi_set_ptr(arg, &cbp); break;
			case 's': ffi_set_ptr(arg, mjs_to_str(p->vm, av, 0)); break;
			case 'b': ffi_set_bool(arg, (int) tof(av)); break;
			case 'f': ffi_set_float(arg, tof(av)); break;
			case 'F': ffi_set_double(arg, (double) tof(av)); break;
			default: ffi_set_word(arg, (int) tof(av)); break;
		}
		num_expected_args++;
	}

	if (num_passed_args != num_expected_args) return vm_err(p->vm, "ffi call %s: %d vs %d", cf->decl, num_expected_args, num_passed_args);

	ffi_call(cf->fn, num_passed_args, &args[0], &args[1]);
	switch (cf->decl[0]) {
		case 's': v = mk_str(p->vm, (char *) args[0].v.i, -1); break;
		case 'f': v = tov(args[0].v.f); break;
		case 'F': v = tov((float) args[0].v.d); break;
		default: v = tov((float) args[0].v.i); break;
	}
  // clang-format on
        while (vm_top(p->vm) > top) vm_drop(p->vm);  // Abandon pushed args
        vm_drop(p->vm);                              // Abandon function object
        LOG((DBGPREFIX "%s: %d\n", __func__, p->tok.tok));
        return vm_push(p->vm, v);  // Push call result
}

static val_t parse_call_dot_mem(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(parse_literal(p, p->tok.tok));
  while (p->tok.tok == '.' || p->tok.tok == '(' || p->tok.tok == '[') {
    if (p->tok.tok == '[') {
      tok_t prev_tok = p->prev_tok;
      pnext(p);
      TRY(parse_expr(p));
      // emit_byte(p, OP_SWAP);
      EXPECT(p, ']');
      pnext(p);
      if (!findtok(s_assign_ops, p->tok.tok) &&
          !findtok(s_postfix_ops, p->tok.tok) &&
          !findtok(s_postfix_ops, prev_tok)) {
        // emit_byte(p, OP_GET);
      }
    } else if (p->tok.tok == '(') {
      pnext(p);
      if (p->noexec) {
        while (p->tok.tok != ')') {
          TRY(parse_expr(p));
          if (p->tok.tok == ',') pnext(p);
        }
        return res;
      } else {
        val_t f = *vm_top(p->vm);
        mjs_type_t t = mjs_type(f);
        if (t == MJS_TYPE_FUNCTION) {
          res = call_js_function(p, f);
        } else if (t == MJS_TYPE_C_FUNCTION) {
          res = call_c_function(p, f);
        } else {
          res = vm_err(p->vm, "calling non-func");
        }
      }
      EXPECT(p, ')');
      pnext(p);
    } else if (p->tok.tok == '.') {
      val_t v = *vm_top(p->vm);
      pnext(p);
      if (!p->noexec) {
        if (p->tok.len == 6 && memcmp(p->tok.ptr, "length", 6) == 0 &&
            mjs_type(v) == MJS_TYPE_STRING) {
          len_t len;
          mjs_to_str(p->vm, v, &len);
          vm_drop(p->vm);
          res = vm_push(p->vm, tov(len));
        } else if (mjs_type(v) != MJS_TYPE_OBJECT) {
          res = vm_push(p->vm, vm_err(p->vm, "lookup in non-obj"));
        } else {
          val_t *prop = findprop(p->vm, v, p->tok.ptr, p->tok.len);
          vm_drop(p->vm);
          res = vm_push(p->vm, prop == NULL ? MJS_UNDEFINED : *prop);
        }
      }
      pnext(p);
    }
  }
  (void) prev_op;
  return res;
}

static val_t parse_postfix(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(parse_call_dot_mem(p, prev_op));
  if (p->tok.tok == DT('+', '+') || p->tok.tok == DT('-', '-')) {
    int op = p->tok.tok == DT('+', '+') ? TOK_POSTFIX_PLUS : TOK_POSTFIX_MINUS;
    TRY(do_op(p, op));
    pnext(p);
  }
  return res;
}

static val_t parse_unary(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  int op = TOK_EOF;
  if (findtok(s_unary_ops, p->tok.tok) != TOK_EOF) {
    op = p->tok.tok;
    pnext(p);
  }
  if (findtok(s_unary_ops, p->tok.tok) != TOK_EOF) {
    res = parse_unary(p, prev_op);
  } else {
    res = parse_postfix(p, prev_op);
  }
  if (res == MJS_ERROR) return res;
  if (op != TOK_EOF) {
    if (op == '-') op = TOK_UNARY_MINUS;
    if (op == '+') op = TOK_UNARY_PLUS;
    do_op(p, op);
  }
  return res;
}

static val_t parse_mul_div_rem(struct parser *p, int prev_op) {
  tok_t ops[] = {'*', '/', '%', TOK_EOF};
  return parse_ltr_binop(p, parse_unary, parse_mul_div_rem, ops, prev_op);
}

static val_t parse_plus_minus(struct parser *p, int prev_op) {
  tok_t ops[] = {'+', '-', TOK_EOF};
  return parse_ltr_binop(p, parse_mul_div_rem, parse_plus_minus, ops, prev_op);
}

static val_t parse_shifts(struct parser *p, int prev_op) {
  tok_t ops[] = {DT('<', '<'), DT('>', '>'), TT('>', '>', '>'), TOK_EOF};
  return parse_ltr_binop(p, parse_plus_minus, parse_shifts, ops, prev_op);
}

static val_t parse_comparison(struct parser *p, int prev_op) {
  return parse_ltr_binop(p, parse_shifts, parse_comparison, s_cmp_ops, prev_op);
}

static val_t parse_equality(struct parser *p, int prev_op) {
  return parse_ltr_binop(p, parse_comparison, parse_equality, s_equality_ops,
                         prev_op);
}

static val_t parse_bitwise_and(struct parser *p, int prev_op) {
  tok_t ops[] = {'&', TOK_EOF};
  return parse_ltr_binop(p, parse_equality, parse_bitwise_and, ops, prev_op);
}

static val_t parse_bitwise_xor(struct parser *p, int prev_op) {
  tok_t ops[] = {'^', TOK_EOF};
  return parse_ltr_binop(p, parse_bitwise_and, parse_bitwise_xor, ops, prev_op);
}

static val_t parse_bitwise_or(struct parser *p, int prev_op) {
  tok_t ops[] = {'|', TOK_EOF};
  return parse_ltr_binop(p, parse_bitwise_xor, parse_bitwise_or, ops, prev_op);
}

static val_t parse_logical_and(struct parser *p, int prev_op) {
  tok_t ops[] = {DT('&', '&'), TOK_EOF};
  return parse_ltr_binop(p, parse_bitwise_or, parse_logical_and, ops, prev_op);
}

static val_t parse_logical_or(struct parser *p, int prev_op) {
  tok_t ops[] = {DT('|', '|'), TOK_EOF};
  return parse_ltr_binop(p, parse_logical_and, parse_logical_or, ops, prev_op);
}

static val_t parse_ternary(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(parse_logical_or(p, TOK_EOF));
  if (prev_op != TOK_EOF) do_op(p, prev_op);
  if (p->tok.tok == '?') {
    pnext(p);
    TRY(parse_ternary(p, TOK_EOF));
    EXPECT(p, ':');
    pnext(p);
    TRY(parse_ternary(p, TOK_EOF));
  }
  return res;
}

static val_t parse_assignment(struct parser *p, int pop) {
  return parse_rtl_binop(p, parse_ternary, parse_assignment, s_assign_ops, pop);
}

static val_t parse_expr(struct parser *p) {
  return parse_assignment(p, TOK_EOF);
}

static val_t parse_let(struct parser *p) {
  val_t res = MJS_TRUE;
  pnext(p);
  for (;;) {
    struct tok tmp = p->tok;
    val_t obj = p->vm->call_stack[p->vm->csp - 1], key, val = MJS_UNDEFINED;
    if (p->tok.tok != TOK_IDENT) return vm_err(p->vm, "indent expected");
    if (findprop(p->vm, obj, p->tok.ptr, p->tok.len) != NULL) {
      return vm_err(p->vm, "[%.*s] already declared", p->tok.len, p->tok.ptr);
    }
    pnext(p);
    if (p->tok.tok == '=') {
      pnext(p);
      TRY(parse_expr(p));
      val = *vm_top(p->vm);
    } else if (!p->noexec) {
      vm_push(p->vm, val);
    }
    key = mk_str(p->vm, tmp.ptr, tmp.len);
    TRY(key);
    TRY(mjs_set(p->vm, obj, key, val));
    // LOG((DBGPREFIX "%s: sp %d, %d\n", __func__, p->vm->sp, p->tok.tok));
    if (p->tok.tok == ',') {
      TRY(vm_drop(p->vm));
      pnext(p);
    }
    if (p->tok.tok == ';' || p->tok.tok == TOK_EOF) break;
  }
  return res;
}

static val_t parse_return(struct parser *p) {
  val_t res = MJS_TRUE;
  pnext(p);
  // It is either "return;" or "return EXPR;"
  if (p->tok.tok == ';' || p->tok.tok == '}') {
    if (!p->noexec) vm_push(p->vm, MJS_UNDEFINED);
  } else {
    res = parse_expr(p);
  }
  // Point parser to the end of func body, so that parse_block() gets '}'
  if (!p->noexec) p->pos = p->end - 1;
  return res;
}

static val_t parse_block_or_stmt(struct parser *p, int create_scope) {
  if (lookahead(p) == '{') {
    return parse_block(p, create_scope);
  } else {
    return parse_statement(p);
  }
}

static val_t parse_while(struct parser *p) {
  val_t res = MJS_TRUE;
  struct parser tmp;
  pnext(p);
  EXPECT(p, '(');
  pnext(p);
  tmp = *p;  // Remember the location of the condition expression
  for (;;) {
    *p = tmp;  // On each iteration, re-evaluate the condition
    TRY(parse_expr(p));
    EXPECT(p, ')');
    pnext(p);
    if (is_true(p->vm, *vm_top(p->vm))) {
      // Condition is true. Drop evaluated condition expression from the stack
      if (!p->noexec) vm_drop(p->vm);
    } else {
      // TODO(lsm): optimise here, p->pos = post_condition if it is saved
      p->noexec++;
      LOG((DBGPREFIX "%s: FALSE!!.., sp %d\n", __func__, p->vm->sp));
    }
    TRY(parse_block_or_stmt(p, 1));
    LOG((DBGPREFIX "%s: done.., sp %d\n", __func__, p->vm->sp));
    if (p->noexec) break;
    vm_drop(p->vm);
    // vm_dump(p->vm);
  }
  LOG((DBGPREFIX "%s: out.., sp %d\n", __func__, p->vm->sp));
  p->noexec = tmp.noexec;
  return res;
}

static val_t parse_if(struct parser *p) {
  val_t res = MJS_TRUE;
  int saved_noexec = p->noexec, cond;
  pnext(p);
  EXPECT(p, '(');
  pnext(p);
  TRY(parse_expr(p));
  EXPECT(p, ')');
  pnext(p);
  if (!p->noexec) {
    cond = is_true(p->vm, *vm_top(p->vm));
    vm_drop(p->vm);
    if (!cond) {
      vm_push(p->vm, MJS_UNDEFINED);
      p->noexec++;
    }
  }
  TRY(parse_block_or_stmt(p, 1));
  p->noexec = saved_noexec;
  return res;
}

static val_t parse_statement(struct parser *p) {
  switch (p->tok.tok) {
    case ';':
      pnext(p);
      return MJS_TRUE;
    case TOK_LET:
      return parse_let(p);
    case '{': {
      val_t res = parse_block(p, 1);
      pnext(p);
      return res;
    }
    case TOK_RETURN:
      return parse_return(p);
    case TOK_WHILE:
      return parse_while(p);
// clang-format off
#if 0
    case TOK_FOR: return parse_for(p);
    case TOK_BREAK: pnext1(p); return MJS_SUCCESS;
    case TOK_CONTINUE: pnext1(p); return MJS_SUCCESS;
#endif
    case TOK_IF: return parse_if(p);
    case TOK_CASE: case TOK_CATCH: case TOK_DELETE: case TOK_DO:
    case TOK_INSTANCEOF: case TOK_NEW: case TOK_SWITCH: case TOK_THROW:
    case TOK_TRY: case TOK_VAR: case TOK_VOID: case TOK_WITH:
      // clang-format on
      return vm_err(p->vm, "[%.*s] not implemented", p->tok.len, p->tok.ptr);
    default: {
      val_t res = MJS_TRUE;
      for (;;) {
        TRY(parse_expr(p));
        if (p->tok.tok != ',') break;
        pnext(p);
      }
      return res;
    }
  }
}

static val_t parse_statement_list(struct parser *p, tok_t endtok) {
  val_t res = MJS_TRUE;
  pnext(p);
  LOG((DBGPREFIX "%s: tok %d endtok %d\n", __func__, p->tok.tok, endtok));
  while (res != MJS_ERROR && p->tok.tok != TOK_EOF && p->tok.tok != endtok) {
    // Drop previous value from the stack
    if (!p->noexec && p->vm->sp > 0) vm_drop(p->vm);
    res = parse_statement(p);
    while (p->tok.tok == ';') pnext(p);
  }
  if (!p->noexec && p->vm->sp == 0) vm_push(p->vm, MJS_UNDEFINED);
  return res;
}

/////////////////////////////// EXTERNAL API /////////////////////////////////

static struct vm *mjs_create(void) {
  struct vm *vm = (struct mjs *) calloc(1, sizeof(*vm));
  vm->objs[0].flags = OBJ_ALLOCATED;
  vm->objs[0].props = INVALID_INDEX;
  vm->call_stack[0] = MK_VAL(MJS_TYPE_OBJECT, 0);
  vm->csp++;
  LOG((DBGPREFIX "%s: size %d bytes\n", __func__, (int) sizeof(*vm)));
  return vm;
};

static void mjs_destroy(struct vm *vm) { free(vm); }

static val_t mjs_eval(struct vm *vm, const char *buf, int len) {
  struct parser p = mk_parser(vm, buf, len > 0 ? len : (int) strlen(buf));
  val_t v = MJS_ERROR;
  vm->error_message[0] = '\0';
  if (parse_statement_list(&p, TOK_EOF) != MJS_ERROR && vm->sp == 1) {
    v = *vm_top(vm);
  }
  vm_dump(vm);
  LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, *vm_top(vm))));
  return v;
}

static val_t mjs_mk_c_func(struct vm *vm, cfn_t fn, const char *decl) {
  val_t v = mk_cfunc(vm);
  struct cfunc *cfunc = &vm->cfuncs[VAL_PAYLOAD(v)];
  if (v == MJS_ERROR) return v;
  if (decl == NULL || decl[0] == '\0') return vm_err(vm, "wrong type spec");
  cfunc->fn = fn;
  cfunc->decl = decl;
  return v;
}

static val_t mjs_ffi(struct vm *vm, const char *p, cfn_t f, const char *s) {
  val_t v = mjs_get_global(vm);
  return mjs_set(mjs, v, mjs_mk_str(vm, p, -1), mjs_mk_c_func(vm, f, s));
}

#endif  // MJS_H
