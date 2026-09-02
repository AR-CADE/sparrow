#ifndef HANDLE_MAP_H
#define HANDLE_MAP_H

#include <cstdint>
#include <unordered_map>

namespace sparrow
{
template<typename T = void>
class HandleMap
{
  private:
    uint32_t next_id_ = 1;
    std::unordered_map<uint32_t, T*> entries_;

  public:
    HandleMap()  = default;
    ~HandleMap() = default;

    HandleMap(const HandleMap &) = delete;
    HandleMap&operator =(const HandleMap &) = delete;

    HandleMap(HandleMap &&) noexcept = default;
    HandleMap&operator =(HandleMap &&) noexcept = default;

    uint32_t add(T *value)
    {
        uint32_t key;
        do {
            key = next_id_++;
            if (next_id_ == 0)
            {
                next_id_ = 1;
            }
        } while (entries_.find(key) != entries_.end());

        entries_[key] = value;
        return key;
    }

    bool remove(uint32_t handle)
    {
        return entries_.erase(handle) > 0;
    }

    T *get(uint32_t handle) const
    {
        auto it = entries_.find(handle);
        if (it != entries_.end())
        {
            return it->second;
        }

        return nullptr;
    }

    bool get(uint32_t handle, T **out) const
    {
        auto it = entries_.find(handle);
        if (it != entries_.end())
        {
            if (out)
            {
                *out = it->second;
            }

            return true;
        }

        return false;
    }

    bool contains(uint32_t handle) const
    {
        return entries_.find(handle) != entries_.end();
    }

    size_t size() const noexcept
    {
        return entries_.size();
    }

    bool empty() const noexcept
    {
        return entries_.empty();
    }

    void clear() noexcept
    {
        entries_.clear();
        next_id_ = 1;
    }

    auto begin() noexcept
    {
        return entries_.begin();
    }

    auto end() noexcept
    {
        return entries_.end();
    }

    auto begin() const noexcept
    {
        return entries_.begin();
    }

    auto end() const noexcept
    {
        return entries_.end();
    }

    auto cbegin() const noexcept
    {
        return entries_.cbegin();
    }

    auto cend() const noexcept
    {
        return entries_.cend();
    }

    template<typename Callback>
    void for_each(Callback && callback) const
    {
        for (const auto &[handle, value] : entries_)
        {
            callback(handle, value);
        }
    }
};
} // namespace sparrow

struct handle_map : public sparrow::HandleMap<void>
{
    using sparrow::HandleMap<void>::HandleMap;
};

struct handle_map_iter
{
    struct handle_map *map = nullptr;
    std::unordered_map<uint32_t, void*>::iterator it;
};

struct handle_map *handle_map_new();
void handle_map_destroy(struct handle_map *map);
uint32_t handle_map_add(struct handle_map *map, void *value);
bool handle_map_get(struct handle_map *map, uint32_t handle, void **out);
bool handle_map_remove(struct handle_map *map, uint32_t handle);

struct handle_map_iter *handle_map_iter_new(struct handle_map *map);
void handle_map_iter_destroy(struct handle_map_iter *iter);
bool handle_map_iter_next(struct handle_map_iter *iter, uint32_t *handle_out,
    void **value_out);

#endif
