#include <atomic>
#include <array>

namespace Homework {

template<typename T, size_t Capacity> 
class ringbuffer
{
private:
  std::array<T, Capacity> container{}; 
  std::atomic<int> head_{0};
  std::atomic<int> tail_{0};

public:
  
  bool push(T element) noexcept
  {
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    size_t next_tail = get_next(current_tail); 
    if (next_tail == head_.load(std::memory_order_acquire))
    {
      return false;
    }
    container[current_tail] = std::move(element);
    tail_.store(next_tail, std::memory_order_release);
return true;

    }

  bool pop(T & element) noexcept
  {
    size_t current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire))
    {
      return false;
    }
    element = std::move(container[current_head]);
    head_.store(get_next(current_head), std::memory_order_release);
        return true;
    }

    size_t size() noexcept
    {
        auto current_head = head_.load(std::memory_order_acquire);
        auto current_tail = tail_.load(std::memory_order_relaxed);
        if (current_head <= current_tail) return current_tail - current_head;
        else return Capacity + current_tail - current_head;
    }

private:
  size_t get_next(size_t index)
  {
    ++index;
    return index == Capacity ? 0 : index;
  }
};

} // namspace Homework
