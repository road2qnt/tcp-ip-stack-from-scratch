#include "map.h"

static int map_find_index(const Map *map, const char *key)
{
    size_t i;
    if (map == NULL || key == NULL) {
        return -1;
    }

    for (i = 0; i < map->size; i++) {
        if (strcmp(map->keys[i], key) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static void copy_string(char destination[], size_t destination_size, const char *source)
{
    if (destination_size == 0) {
        return;
    }

    strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

void map_init(Map *map)
{
    if (map == NULL) {
        return;
    }

    map->size = 0;
}

int map_insert(Map *map, const char *key, const char *value)
{
    int index;

    if (map == NULL || key == NULL || value == NULL) {
        return 0;
    }

    index = map_find_index(map, key);
    if (index >= 0) {
        copy_string(map->values[index], MAP_MAX_VALUE_LENGTH, value);
        return 1;
    }

    if (map->size >= MAP_MAX_ENTRIES) {
        return 0;
    }

    copy_string(map->keys[map->size], MAP_MAX_KEY_LENGTH, key);
    copy_string(map->values[map->size], MAP_MAX_VALUE_LENGTH, value);
    map->size++;

    return 1;
}

int map_remove(Map *map, const char *key)
{
    int index;
    size_t i;

    if (map == NULL || key == NULL) {
        return 0;
    }

    index = map_find_index(map, key);
    if (index < 0) {
        return 0;
    }

    for (i = (size_t)index; i + 1 < map->size; i++) {
        copy_string(map->keys[i], MAP_MAX_KEY_LENGTH, map->keys[i + 1]);
        copy_string(map->values[i], MAP_MAX_VALUE_LENGTH, map->values[i + 1]);
    }

    map->size--;
    map->keys[map->size][0] = '\0';
    map->values[map->size][0] = '\0';

    return 1;
}

char *map_get(Map *map, const char *key)
{
    int index = map_find_index(map, key);

    if (index < 0) {
        return NULL;
    }

    return map->values[index];
}

const char *map_get_const(const Map *map, const char *key)
{
    int index = map_find_index(map, key);

    if (index < 0) {
        return NULL;
    }

    return map->values[index];
}
