#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

struct ht_entry{
    const char * key;
    void * value;

};

struct ht {
    struct ht_entry * entries;
    size_t capacity;
    size_t length;
};

struct ht* ht_make();
void ht_destroy(struct ht* table);
void * ht_get(struct ht *table, const char * key);
const char * ht_set(struct ht * table, const char * key, void* value);

#endif //HASHHTABLE_H


