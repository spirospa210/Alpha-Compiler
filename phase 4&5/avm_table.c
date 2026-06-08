#include <stdint.h>
#include "avm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ---- hashing ---- */
static unsigned hash_num(double d) {
    unsigned long long u; memcpy(&u, &d, sizeof u);
    return (unsigned)((u ^ (u >> 32)) % AVM_TABLE_HASHSIZE);
}
static unsigned hash_str(const char* s) {
    unsigned h = 5381; int c;
    while ((c = *s++)) h = ((h << 5) + h) + c;
    return h % AVM_TABLE_HASHSIZE;
}
static unsigned hash_ptr(void* p) {
    return (unsigned)(((uintptr_t)p >> 4) % AVM_TABLE_HASHSIZE);
}

avm_table* avm_tablenew(void) {
    avm_table* t = calloc(1, sizeof(avm_table));
    t->refCounter = 0;
    t->total = 0;
    t->orderHead = t->orderTail = NULL;
    return t;
}

void avm_tableincrefcounter(avm_table* t) { ++t->refCounter; }

static void bucket_clear_list(avm_table_bucket* head) {
    while (head) {
        avm_table_bucket* nx = head->next;
        avm_memcellclear(&head->key);
        avm_memcellclear(&head->value);
        free(head);
        head = nx;
    }
}

void avm_tabledestroy(avm_table* t) {
    for (unsigned i=0;i<AVM_TABLE_HASHSIZE;i++) {
        bucket_clear_list(t->numIndexed[i]);
        bucket_clear_list(t->strIndexed[i]);
        bucket_clear_list(t->userIndexed[i]);
        bucket_clear_list(t->libIndexed[i]);
        bucket_clear_list(t->tableIndexed[i]);
    }
    bucket_clear_list(t->boolIndexed[0]);
    bucket_clear_list(t->boolIndexed[1]);
    free(t);
}

void avm_tabledecrefcounter(avm_table* t) {
    assert(t->refCounter > 0);
    if (--t->refCounter == 0) avm_tabledestroy(t);
}

/* select the right bucket-array head pointer slot for a key */
static avm_table_bucket** bucket_slot(avm_table* t, avm_memcell* key) {
    switch (key->type) {
        case number_m: return &t->numIndexed [hash_num(key->data.numVal)];
        case string_m: return &t->strIndexed [hash_str(key->data.strVal)];
        case bool_m:   return &t->boolIndexed[key->data.boolVal ? 1 : 0];
        case userfunc_m: return &t->userIndexed[key->data.funcVal % AVM_TABLE_HASHSIZE];
        case libfunc_m:  return &t->libIndexed [hash_str(key->data.libfuncVal)];
        case table_m:    return &t->tableIndexed[hash_ptr(key->data.tableVal)];
        default: return NULL;   /* nil/undef not allowed as index */
    }
}

static int key_equal(avm_memcell* a, avm_memcell* b) {
    if (a->type != b->type) return 0;
    switch (a->type) {
        case number_m:   return a->data.numVal == b->data.numVal;
        case string_m:   return strcmp(a->data.strVal, b->data.strVal) == 0;
        case bool_m:     return a->data.boolVal == b->data.boolVal;
        case userfunc_m: return a->data.funcVal == b->data.funcVal;
        case libfunc_m:  return strcmp(a->data.libfuncVal, b->data.libfuncVal) == 0;
        case table_m:    return a->data.tableVal == b->data.tableVal;  /* identity */
        default:         return 0;
    }
}

static avm_table_bucket* find_bucket(avm_table* t, avm_memcell* key) {
    avm_table_bucket** slot = bucket_slot(t, key);
    if (!slot) return NULL;
    for (avm_table_bucket* b = *slot; b; b = b->next)
        if (key_equal(&b->key, key)) return b;
    return NULL;
}

avm_memcell* avm_tablegetelem(avm_table* t, avm_memcell* key) {
    avm_table_bucket* b = find_bucket(t, key);
    return b ? &b->value : NULL;
}

/* remove a bucket (used when setting a value to nil) */
static void order_unlink(avm_table* t, avm_table_bucket* victim) {
    avm_table_bucket* prev = NULL;
    for (avm_table_bucket* b = t->orderHead; b; prev=b, b=b->orderNext) {
        if (b == victim) {
            if (prev) prev->orderNext = b->orderNext;
            else      t->orderHead    = b->orderNext;
            if (t->orderTail == b) t->orderTail = prev;
            return;
        }
    }
}

static void table_remove(avm_table* t, avm_memcell* key) {
    avm_table_bucket** slot = bucket_slot(t, key);
    if (!slot) return;
    avm_table_bucket* prev = NULL;
    for (avm_table_bucket* b = *slot; b; prev=b, b=b->next) {
        if (key_equal(&b->key, key)) {
            if (prev) prev->next = b->next; else *slot = b->next;
            order_unlink(t, b);
            avm_memcellclear(&b->key);
            avm_memcellclear(&b->value);
            free(b);
            t->total--;
            return;
        }
    }
}

void avm_tablesetelem(avm_table* t, avm_memcell* key, avm_memcell* value) {
    /* nil/undef index is a runtime error (handled by caller execute_tablesetelem) */
    /* setting value to nil => delete element */
    if (value->type == nil_m) { table_remove(t, key); return; }

    avm_table_bucket* b = find_bucket(t, key);
    if (b) {
        avm_assign(&b->value, value);
        return;
    }
    /* new bucket */
    avm_table_bucket** slot = bucket_slot(t, key);
    if (!slot) return;
    b = calloc(1, sizeof(avm_table_bucket));
    b->key.type = undef_m; b->value.type = undef_m;
    avm_assign(&b->key, key);
    avm_assign(&b->value, value);
    b->next = *slot; *slot = b;
    /* insertion order */
    b->orderNext = NULL;
    if (t->orderTail) t->orderTail->orderNext = b; else t->orderHead = b;
    t->orderTail = b;
    t->total++;
}
