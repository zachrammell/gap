#pragma once

#include <cassert>

#include <forward_list>

namespace ListHelpers
{
    template <typename T>
    void remove_list_element(std::forward_list<T>* list, T* elm)
    {
        auto prev = list->before_begin();
        auto first = begin(*list);
        auto last = end(*list);
        bool removed = false;
        for (; first != last; ++first, ++prev)
        {
            if (&*first == elm)
            {
                list->erase_after(prev);
                removed = true;
                break;
            }
        }
        assert(removed);
    }
} // namespace ListHelpers