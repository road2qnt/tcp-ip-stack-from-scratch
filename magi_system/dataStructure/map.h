#ifndef MAP_H
#define MAP_H

#include "../core/mac_ip.h"

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

// Map (MacAddress-Int(PortNumber))
typedef struct Map_MAC {
    size_t size;
    MacAddress keys[MAP_MAX_ENTRIES]; // Mac Address
    int values[MAP_MAX_ENTRIES]; // Port Number
} Map_MAC;

void map_mac_init(Map_MAC *map);
int map_mac_insert(Map_MAC *map, const MacAddress *key, int value);
int map_mac_remove(Map_MAC *map, const MacAddress *key);
int *map_mac_get(Map_MAC *map, const MacAddress *key);
const int *map_mac_get_const(const Map_MAC *map, const MacAddress *key);

#endif
