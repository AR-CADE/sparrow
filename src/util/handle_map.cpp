#include "handle_map.hpp"

handle_map *handle_map_new()
{
    return new handle_map();
}

void handle_map_destroy(handle_map *map)
{
    delete map;
}

uint32_t handle_map_add(handle_map *map, void *value)
{
    if (!map)
    {
        return 0;
    }

    return map->add(value);
}

bool handle_map_remove(handle_map *map, uint32_t handle)
{
    if (!map)
    {
        return false;
    }

    return map->remove(handle);
}

bool handle_map_get(handle_map *map, uint32_t handle, void **out)
{
    if (!map)
    {
        return false;
    }

    return map->get(handle, out);
}

handle_map_iter *handle_map_iter_new(handle_map *map)
{
    if (!map)
    {
        return nullptr;
    }

    handle_map_iter *iter = new handle_map_iter();
    iter->map = map;
    iter->it  = map->begin();
    return iter;
}

void handle_map_iter_destroy(handle_map_iter *iter)
{
    delete iter;
}

bool handle_map_iter_next(handle_map_iter *iter, uint32_t *handle_out,
    void **value_out)
{
    if (!iter || !iter->map || (iter->it == iter->map->end()))
    {
        return false;
    }

    if (handle_out != nullptr)
    {
        *handle_out = iter->it->first;
    }

    if (value_out != nullptr)
    {
        *value_out = iter->it->second;
    }

    ++iter->it;
    return true;
}
