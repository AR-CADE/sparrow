#ifndef SPARROW_RAII_HPP
#define SPARROW_RAII_HPP

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace sparrow
{
template<typename T>
struct CFreeDeleter
{
    void operator ()(T *ptr) const noexcept
    {
        free(const_cast<std::remove_cv_t<T>*>(ptr));
    }
};

template<typename T>
using c_unique_ptr = std::unique_ptr<T, CFreeDeleter<T>>;

template<typename T>
inline c_unique_ptr<T> wrap_c_ptr(T *ptr) noexcept
{
    return c_unique_ptr<T>(ptr);
}

template<typename F>
class ScopeGuard
{
  public:
    explicit ScopeGuard(F && func) :
        func_(std::forward<F>(func))
    {}

    ~ScopeGuard()
    {
        if (active_)
        {
            func_();
        }
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard&operator =(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard && other) noexcept :
        func_(std::move(other.func_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

  private:
    F func_;
    bool active_{true};
};

template<typename F>
inline auto make_scope_guard(F && func)
{
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(func));
}
} // namespace sparrow

#endif // SPARROW_RAII_HPP
