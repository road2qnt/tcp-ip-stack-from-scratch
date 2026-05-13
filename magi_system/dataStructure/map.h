#ifndef MAP_H
#define MAP_H

#include <stddef.h>
#include <string.h>

#define MAP_MAX_ENTRIES 100
#define MAP_MAX_KEY_LENGTH 100
#define MAP_MAX_VALUE_LENGTH 100

// Map (Char-Char)
typedef struct Map {
    size_t size;
    char keys[MAP_MAX_ENTRIES][MAP_MAX_KEY_LENGTH];
    char values[MAP_MAX_ENTRIES][MAP_MAX_VALUE_LENGTH];
} Map;

void map_init(Map *map);
int map_insert(Map *map, const char *key, const char *value);
int map_remove(Map *map, const char *key);
char *map_get(Map *map, const char *key);
const char *map_get_const(const Map *map, const char *key);

#endif
