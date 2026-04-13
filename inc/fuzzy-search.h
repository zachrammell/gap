#pragma once

#include <cassert>
#include <cctype>

#include <algorithm>
#include <concepts>
#include <iterator>
#include <string_view>
#include <vector>

#include "arena.h"
#include "utf-8.h"

namespace FuzzySearch
{
    struct FuzzyNameChar
    {
        UTF8::Codepoint c = 0;
        bool matched = false;
    };

    using FuzzyName = std::vector<FuzzyNameChar>;

    struct FuzzyName2
    {
        FuzzyNameChar* name;
        uint64_t size;
    };

    using ConvertedFilter = std::vector<UTF8::Codepoint>;

    struct ConvertedFilter2
    {
        UTF8::Codepoint* conv_array;
        uint64_t size;
    };

    inline FuzzyName make_fuzzy_name(std::string_view s)
    {
        FuzzyName name;
        name.reserve(s.size());
        UTF8::CodepointWalker walker{ s };
        while (not walker.exhausted())
        {
            auto cp = walker.next();
            name.push_back({ .c = cp, .matched = false });
        }
        return name;
    }

    inline FuzzyName2 make_fuzzy_name(Arena::Arena* arena, String8 s)
    {
        FuzzyName2 result{};
        result.name = Arena::push_array<FuzzyNameChar>(arena, s.size);
        UTF8::CodepointWalker walker{ sv_str8(s) };
        while (not walker.exhausted())
        {
            auto cp = walker.next();
            result.name[result.size++] = { .c = cp, .matched = false };
        }
        return result;
    }

    template <typename T>
    concept HasFuzzyName = requires(T t) {
        { t.name } -> std::convertible_to<FuzzyName>;
    };

    template <typename T>
    struct FuzzySearchResult
    {
        T* entry;
        int score;
    };

    template <typename T>
    using FuzzySearchResults = std::vector<FuzzySearchResult<T>>;

    template <typename T>
    struct FuzzySearchResults2
    {
        FuzzySearchResult<T>* results;
        uint64_t size;
    };

    template <HasFuzzyName T>
    using FuzzySearchInput = std::vector<T>;

    struct MultiFuzzyName
    {
        FuzzyName2 name;
        double weight = 1.0;
    };

    template <int N>
    constexpr int multi_fuzzy_name_size(const MultiFuzzyName(&)[N])
    {
        return N;
    }

    template <typename T>
    concept HasMultiFuzzyName = requires(T t) {
        { t.names };
        multi_fuzzy_name_size(t.names) > 0;
        { t.names[0] } -> std::convertible_to<MultiFuzzyName>;
    };

    template <typename T>
    concept FuzzyNameList = requires(T t) {
        { *t.first } -> HasMultiFuzzyName;
    };

    template <HasFuzzyName T>
    void reset_states(FuzzySearchInput<T>* in)
    {
        for (auto& e : *in)
        {
            for (auto& c : e.name)
            {
                c.matched = false;
            }
        }
    }

    template <FuzzyNameList T>
    void reset_multi_states(T* in)
    {
        for EachNode(n, in->first)
        {
            for (auto& e : n->names)
            {
                for EachIndex(i, e.name.size)
                {
                    e.name.name[i].matched = false;
                }
            }
        }
    }

    struct ScoreResult
    {
        int score;
        bool ignore;
    };

    inline ScoreResult score_name(FuzzyName* name, const ConvertedFilter& filter)
    {
        assert(not filter.empty());
        ScoreResult result{};
        // We're going to implement a very simple scoring mechanism similar to that described in
        // https://www.forrestthewoods.com/blog/reverse_engineering_sublime_texts_fuzzy_match/.
        constexpr int unmatched = -1;
        constexpr int consecutive = 5;
        constexpr int unmatched_leading = -3;
        if (filter.size() <= name->size())
        {
            auto first = begin(*name);
            auto last = end(*name);
            auto prev_found = first;
            // Since we can assume that 'filter' is non-empty, we can start off with finding the first entry.
            auto found = std::find_if(first,
                                        last,
                                        [&](auto& c)
                                        {
                                            // Note: for std::tolower to function properly, we need to first convert the argument to
                                            // an unsigned char.
                                            UTF8::Codepoint a = c.c;
                                            UTF8::Codepoint b = filter[0];
                                            if (UTF8::ascii_codepoint(a) and UTF8::ascii_codepoint(b))
                                            {
                                                return std::tolower(a) == std::tolower(b);
                                            }
                                            return a == b;
                                        });
            if (found == last)
            {
                result.ignore = true;
            }
            else
            {
                // Compute the leading character penalty.
                auto dist = std::distance(first, found);
                // We only go to a maximum of 3 according to the article.
                result.score += static_cast<int>(std::min(dist, decltype(dist)(3ll))) * unmatched_leading;
                // We also want to deduct for additional unmatched characters between [first, found).
                if (dist > 3)
                {
                    dist -= 3;
                    result.score += static_cast<int>(dist) * unmatched;
                }
                // Add matching case bonus.
                result.score += found->c == filter[0];
                // Note: We've already tested for the first character.
                found->matched = true;
                first = next(found);
                prev_found = found;
                for (size_t i = 1; i != filter.size(); ++i)
                {
                    found = std::find_if(first,
                                            last,
                                            [&](auto& c)
                                            {
                                                // Note: for std::tolower to function properly, we need to first convert the argument to
                                                // an unsigned char.
                                                UTF8::Codepoint a = c.c;
                                                UTF8::Codepoint b = filter[i];
                                                if (UTF8::ascii_codepoint(a) and UTF8::ascii_codepoint(b))
                                                {
                                                    return std::tolower(a) == std::tolower(b);
                                                }
                                                return a == b;
                                            });
                    if (found == last)
                    {
                        result.ignore = true;
                        break;
                    }
                    // Compute the consecutive bonus.
                    if (std::distance(prev_found, found) == 1)
                    {
                        result.score += consecutive;
                    }
                    else
                    {
                        // Score is always relative distance from where we found the matching char to
                        // where we started.
                        result.score += static_cast<int>(std::distance(first, found)) * unmatched;
                    }

                    // Matching case bonus.
                    result.score += found->c == filter[i];
                    found->matched = true;
                    first = next(found);
                    prev_found = found;
                }
                // Bump the score to account for unmatched characters.
                result.score += static_cast<int>(std::distance(first, last)) * unmatched;
            }
        }
        else
        {
            result.ignore = true;
        }
        return result;
    }

    inline ScoreResult score_name(FuzzyName2* name, const ConvertedFilter2& filter)
    {
        assert(filter.size != 0);
        ScoreResult result{};
        // We're going to implement a very simple scoring mechanism similar to that described in
        // https://www.forrestthewoods.com/blog/reverse_engineering_sublime_texts_fuzzy_match/.
        constexpr int unmatched = -1;
        constexpr int consecutive = 5;
        constexpr int unmatched_leading = -3;
        if (filter.size <= name->size)
        {
            auto first = name->name;
            auto last = first + name->size;
            auto prev_found = first;
            // Since we can assume that 'filter' is non-empty, we can start off with finding the first entry.
            auto found = std::find_if(first,
                                        last,
                                        [&](auto& c)
                                        {
                                            // Note: for std::tolower to function properly, we need to first convert the argument to
                                            // an unsigned char.
                                            UTF8::Codepoint a = c.c;
                                            UTF8::Codepoint b = filter.conv_array[0];
                                            if (UTF8::ascii_codepoint(a) and UTF8::ascii_codepoint(b))
                                            {
                                                return std::tolower(a) == std::tolower(b);
                                            }
                                            return a == b;
                                        });
            if (found == last)
            {
                result.ignore = true;
            }
            else
            {
                // Compute the leading character penalty.
                auto dist = std::distance(first, found);
                // We only go to a maximum of 3 according to the article.
                result.score += static_cast<int>(std::min(dist, decltype(dist)(3ll))) * unmatched_leading;
                // We also want to deduct for additional unmatched characters between [first, found).
                if (dist > 3)
                {
                    dist -= 3;
                    result.score += static_cast<int>(dist) * unmatched;
                }
                // Add matching case bonus.
                result.score += found->c == filter.conv_array[0];
                // Note: We've already tested for the first character.
                found->matched = true;
                first = found + 1;
                prev_found = found;
                for (size_t i = 1; i != filter.size; ++i)
                {
                    found = std::find_if(first,
                                            last,
                                            [&](auto& c)
                                            {
                                                // Note: for std::tolower to function properly, we need to first convert the argument to
                                                // an unsigned char.
                                                UTF8::Codepoint a = c.c;
                                                UTF8::Codepoint b = filter.conv_array[i];
                                                if (UTF8::ascii_codepoint(a) and UTF8::ascii_codepoint(b))
                                                {
                                                    return std::tolower(a) == std::tolower(b);
                                                }
                                                return a == b;
                                            });
                    if (found == last)
                    {
                        result.ignore = true;
                        break;
                    }
                    // Compute the consecutive bonus.
                    if (std::distance(prev_found, found) == 1)
                    {
                        result.score += consecutive;
                    }
                    else
                    {
                        // Score is always relative distance from where we found the matching char to
                        // where we started.
                        result.score += static_cast<int>(std::distance(first, found)) * unmatched;
                    }

                    // Matching case bonus.
                    result.score += found->c == filter.conv_array[i];
                    found->matched = true;
                    first = found + 1;
                    prev_found = found;
                }
                // Bump the score to account for unmatched characters.
                result.score += static_cast<int>(std::distance(first, last)) * unmatched;
            }
        }
        else
        {
            result.ignore = true;
        }
        return result;
    }

    // We mutate the 'name' directly in 'entries', so we need a mutable reference.
    template <HasFuzzyName T>
    void populate_fuzzy_search_results(FuzzySearchInput<T>* entries, FuzzySearchResults<T>* result, std::string_view raw_filter)
    {
        assert(not raw_filter.empty());
        reset_states(entries);
        result->clear();
        result->reserve(entries->size());
        // We need to convert the filter.
        ConvertedFilter filter;
        {
            UTF8::CodepointWalker walker{ raw_filter };
            while (not walker.exhausted())
            {
                filter.push_back(walker.next());
            }
        }
        // We want to perform a fuzzy search, so we want to ignore case, if possible, and only match character positions.
        for (auto& e : *entries)
        {
            auto scored = score_name(&e.name, filter);
            if (not scored.ignore)
            {
                result->push_back({ .entry = &e, .score = scored.score });
            }
        }

        // Finally, sort the result by score (lower is better).
        std::sort(begin(*result),
                    end(*result),
                    [](const auto& a, const auto& b)
                    {
                        return a.score > b.score;
                    });
    }

    // We mutate the 'name' directly in 'entries', so we need a mutable reference.
    template <FuzzyNameList L, typename T>
    void populate_multi_fuzzy_search_results(Arena::Arena* arena, L* entries, FuzzySearchResults2<T>* result, std::string_view raw_filter)
    {
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        assert(not raw_filter.empty());
        reset_multi_states(entries);
        *result = {};
        result->results = Arena::push_array<FuzzySearchResult<T>>(arena, entries->count);
        // We need to convert the filter.
        ConvertedFilter2 filter{};
        filter.conv_array = Arena::push_array<UTF8::Codepoint>(scratch.arena, raw_filter.size());
        {
            UTF8::CodepointWalker walker{ raw_filter };
            while (not walker.exhausted())
            {
                filter.conv_array[filter.size++] = walker.next();
            }
        }
        // We want to perform a fuzzy search, so we want to ignore case, if possible, and only match character positions.
        constexpr int sz = multi_fuzzy_name_size(T{}.names);
        for EachNode(e, entries->first)
        {
            int score = 0;
            bool ignore = true;

            for EachIndex(i, sz)
            {
                auto scored = score_name(&e->names[i].name, filter);
                ignore = ignore and scored.ignore;
                score += scored.score * not scored.ignore;
            }

            if (not ignore)
            {
                result->results[result->size++] = { .entry = e, .score = score };
            }
        }
        // Finally, sort the result by score (lower is better).
        std::sort(result->results,
                    result->results + result->size,
                    [](const auto& a, const auto& b)
                    {
                        return a.score > b.score;
                    });
        Arena::scratch_end(scratch);
    }
} // namespace FuzzySearch
