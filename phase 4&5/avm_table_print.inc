/* avm_table_print.inc - included at end of avm_core.c
 * Format (from libFunctions.asc):
 *   [{key1 : 1}, {key2 : 2}, {key5 : [{1 : 3}, {2 : 4}, ]}, ]
 * - strings printed WITHOUT quotes (both keys and values)
 * - trailing ", " after EVERY element, then closing ]
 * - nested tables recurse
 * - iterate in insertion order
 */

/* key/value to string but strings are UNQUOTED inside tables */
static char* elem_tostring(avm_memcell* m) {
    if (m->type == string_m) return strdup(m->data.strVal);    /* unquoted */
    return avm_tostring(m);                                     /* recurse/normal */
}

static char* table_tostring(avm_table* t) {
    /* build into a dynamic buffer */
    size_t cap = 256, len = 0;
    char* out = malloc(cap);
    out[0] = 0;

    #define APPEND(s) do { \
        const char* _s = (s); size_t _l = strlen(_s); \
        while (len + _l + 1 > cap) { cap *= 2; out = realloc(out, cap); } \
        memcpy(out + len, _s, _l + 1); len += _l; \
    } while (0)

    APPEND("[");
    for (avm_table_bucket* b = t->orderHead; b; b = b->orderNext) {
        char* ks = elem_tostring(&b->key);
        char* vs = elem_tostring(&b->value);
        APPEND("{");
        APPEND(ks);
        APPEND(" : ");
        APPEND(vs);
        APPEND("}, ");
        free(ks); free(vs);
    }
    APPEND("]");
    #undef APPEND
    return out;
}
