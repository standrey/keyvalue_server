#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

#define INITIAL_CAPACITY 16
#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

struct ht* ht_make() {
    struct ht * table = (struct ht *)calloc(1, sizeof(struct ht));
    if (table == NULL) {
        fprintf(stderr, "can't allocate hash table structure\n");
        return NULL;
    }

    table->capacity = INITIAL_CAPACITY;

    table->entries = (struct ht_entry * )calloc(table->capacity, sizeof(struct ht_entry));
    if (table->entries == NULL) {
        fprintf(stderr, "can't allocate hash table entries\n");
        free(table);
        return NULL;
    }
    return table;
}

void ht_destroy(struct ht* table) {
    size_t i;
    for (i =0; i< table->capacity; ++i) {
        free((void*)table->entries[i].key);
    }
    free(table->entries);
    free(table);
}

static uint64_t hash_key(const char * key) {
    uint64_t hash = FNV_OFFSET;
    for (const char * p = key; *p; ++p) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

void * ht_get(struct ht *table, const char * key) {
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(table->capacity - 1));

    while ( table->entries[index].key != NULL) {
        if(strcmp(key, table->entries[index].key) == 0) {
           return table->entries[index].value;    
        }
        ++index;
        if(index >= table->capacity) {
            index = 0 ;
        }
    }
    return NULL;
}

static const char* ht_set_entry(struct ht_entry * entries, size_t capacity, const char * key, void * value, size_t * length) {
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(capacity -1 ));

    while (entries[index].key != NULL ) {
        if (strcmp(key, entries[index].key) == 0) {
            entries[index].value = value;
            return entries[index].key;
        }
        index++;
        if (index >= capacity) {
            index = 0;
        }
    }

    if(length != NULL) {
        key = strdup(key);
        if (key == NULL) {
            fprintf(stderr, "can't duplicate the key [%s] before insertion", key);
            return NULL;
        }
    }
        (*length)++;
        entries[index].key = (char*)key;
        entries[index].value = value;
    return key;
}

static bool ht_expand(struct ht* table) {
    size_t new_capacity = table->capacity * 1.5;
    if(new_capacity < table->capacity) { 
        fprintf(stderr, "can't increase hash table capacity");
        return false;
    }
    struct ht_entry * new_entries = (struct ht_entry*)calloc(new_capacity, sizeof(struct ht_entry));
    if(new_entries == NULL) {
        fprintf(stderr,"can't allocate [%lu] bytes for new hash table", new_capacity * sizeof(struct ht_entry));
        return false;
    }
    
    for (size_t i = 0; i < table->capacity; ++i) {
         struct ht_entry entry = table->entries[i];
         if(entry.key != NULL) {
            ht_set_entry(new_entries, new_capacity, entry.key, entry.value, NULL);
        }
    }

    free(table->entries);
    table->entries = new_entries;
    table->capacity = new_capacity;
    return true;
}

const char * ht_set(struct ht * table, const char * key, void* value) {
    assert(value != NULL);
    if(value == NULL) {
        return NULL;
    }

    if(table->length >= table->capacity /2) {
        if (!ht_expand(table)) {
            fprintf(stderr, "can't add value [%s] to hashmap, key", key);
            return NULL;
        }
    }

    return ht_set_entry(table->entries, table->capacity, key, value, &table->length);
}

