#pragma once

#include "algos_concepts.hpp"

#include <cstddef> // for ptrdiff_t
#include <utility> // for pair

// TODO(nicolas): compare / merge with things from eop

namespace algos
{

template <typename T> T &max(T &a, T &b)
{
        if (b < a) {
                return a;
        }
        return b;
}

template <typename T> const T &max(const T &a, const T &b)
{
        if (b < a) {
                return a;
        }
        return b;
}
}

// Integral types
namespace algos
{
template <Integral I> bool zero(I i) { return i == 0; };

template <Integral I> I successor(I i) { return i + 1; }

template <Integral I> I predecessor(I i) { return i - 1; }
}

// Pointer types
namespace algos
{
template <typename T> struct ReadableConcept<T *> {
        using value_type = T;
};

// # pointer as Readable
template <typename T> const T &source(const T *x) { return *x; }

template <typename T> T &source(T *x) { return *x; }

// # pointer as Iterator

template <typename T> struct IteratorConcept<T *> {
        using difference_type = std::ptrdiff_t;
};

template <typename T> const T *successor(const T *x) { return x + 1; }

template <typename T> T *successor(T *x) { return x + 1; }

// # pointer as BidirectionalIterator

template <typename T> const T *predecessor(const T *x) { return x - 1; }

template <typename T> T *predecessor(T *x) { return x - 1; }

// # pointer as Writable
template <typename T> T &sink(T *x) { return *x; }
}

// Array types
namespace algos
{
template <typename T, size_t N> struct SequenceConcept<T(&)[N]> {
        using iterator_type = T *;
        using const_iterator_type = T const *;
};

template <typename T, size_t N> constexpr T *begin(T(&array)[N])
{
        return array;
}

template <typename T, size_t N> constexpr T *end(T(&array)[N])
{
        return array + N;
}
}

namespace algos
{
template <Iterator I> bool empty(I x, I y) { return x == y; }
template <Sequence S> bool empty(S s) { return empty(begin(s), end(s)); }
}

// # algorithms
namespace algos
{

template <Iterator I, UnaryPredicate P, Iterator J>
J count_unguarded(I f, P p, J j)
{
        while (p(source(f))) {
                j = successor(j);
                f = successor(f);
        }

        return j;
}

template <Iterator I, UnaryPredicate P>
typename IteratorConcept<I>::difference_type count_unguarded(I f, P p)
{
        return count_unguarded(f, p,
                               typename IteratorConcept<I>::difference_type(0));
}

/**
@requires Domain(Pred) = ValueType(It)
*/
template <Iterator It, UnaryPredicate Pred>
It find_if(It first, It last, Pred pred)
{
        while (first != last) {
                if (pred(*first)) {
                        break;
                }
                first = successor(first);
        }

        return first;
}

/**
@requires Domain(Pred) = ValueType(It)
@requires Domain(ValidItPred) = ValueType(It)
*/
template <Iterator It, UnaryPredicate ValidItPred, UnaryPredicate Pred>
It find_if_unguarded(It first, ValidItPred valid, Pred pred)
{
        while (valid(*first)) {
                if (pred(*first)) {
                        break;
                }
                first = successor(first);
        }

        return first;
}

/**
@requires Domain(ValidItPred) = ValueType(It)
*/
template <Iterator It, UnaryPredicate ValidItPred>
It find_unguarded(It first,
                  ValidItPred valid,
                  typename ReadableConcept<It>::value_type const &x)
{
        return find_if_unguarded(
            first, valid,
            [x](typename ReadableConcept<It>::value_type const &y) {
                    return x == y;
            });
}

/**
@requires Domain(Pred) = ValueType(It)
@requires Domain(ValidItPred) = ValueType(It)
*/
template <Iterator It, UnaryPredicate ValidItPred, UnaryPredicate Pred>
It find_last_if_unguarded(It first, ValidItPred valid, Pred pred)
{
        It y_prime = find_if_unguarded(first, valid, pred);
        if (!valid(source(y_prime))) {
                return y_prime;
        }

        It y;
        do {
                y = y_prime;
                y_prime = find_if_unguarded(successor(y), valid, pred);
        } while (valid(source(y_prime)));

        return y;
}

/**
@requires Domain(Pred) = ValueType(It)
@requires Domain(ValidItPred) = ValueType(It)
*/
template <Iterator It, UnaryPredicate ValidItPred>
It find_last_unguarded(It first,
                       ValidItPred valid,
                       typename ReadableConcept<It>::value_type const &x)
{
        return find_last_if_unguarded(
            first, valid,
            [x](typename ReadableConcept<It>::value_type const &y) {
                    return x == y;
            });
}

/**
@requires Domain(Pred) = ValueType(It)
*/
template <BidirectionalIterator It, UnaryPredicate Pred>
It find_if_backward(It first, It last, Pred pred)
{
        while (first != last) {
                last = predecessor(last);
                if (pred(*last)) {
                        return last;
                }
        }

        return last;
}

template <BidirectionalIterator It>
It find_backward(It first,
                 It last,
                 typename ReadableConcept<It>::value_type const &x)
{
        return find_if_backward(
            first, last,
            [x](typename ReadableConcept<It>::value_type const &y) {
                    return y == x;
            });
}

/**
 * @requires Domain(R) = Domain(P) = ValueType(I0) = ValueType(I1)
 */
template <InputIterator I0,
          InputIterator I1,
          UnaryPredicate P,
          BinaryPredicate R>
std::pair<I0, I1> find_mismatch_unguarded(I0 f0, I1 f1, P guard, R r)
{
        while (guard(source(f0)) && guard(source(f1)) &&
               r(source(f0), source(f1))) {
                f0 = successor(f0);
                f1 = successor(f1);
        }

        return std::make_pair(f0, f1);
}

/**
   @requires Domain(Op) = ValueType(InputIt)
*/
template <Iterator InputIt, UnaryFunction Op>
void for_each(InputIt first, InputIt last, Op operation)
{
        while (first != last) {
                operation(source(first));
                first = successor(first);
        }
}

/**
   @requires Domain(Op) = ValueType(InputIt)
   @pre n >= 0
*/
template <Iterator InputIt, UnaryFunction Op>
InputIt for_each_n(InputIt first,
                   typename IteratorConcept<InputIt>::difference_type n,
                   Op operation)
{
        while (!zero(n)) {
                operation(source(first));
                first = successor(first);
                n = predecessor(n);
        }

        return first;
}

template <Iterator InputIt, OutputIterator OutputIt>
void copy_step(InputIt &f_i, OutputIt &f_o)
{
        sink(f_o) = source(f_i);
        f_o = successor(f_o);
        f_i = successor(f_i);
}

template <Iterator InputIt, OutputIterator OutputIt>
OutputIt copy(InputIt first, InputIt last, OutputIt dest_first)
{
        while (first != last) {
                copy_step(first, dest_first);
        }

        return dest_first;
}

template <Iterator InputIt, OutputIterator OutputIt>
std::pair<InputIt, OutputIt> copy_bounded(InputIt first,
                                          InputIt last,
                                          OutputIt dest_first,
                                          OutputIt dest_last)
{
        while (first != last && dest_first != dest_last) {
                copy_step(first, dest_first);
        }
        return std::make_pair(first, dest_first);
}

template <Iterator InputIt, UnaryPredicate Pred, OutputIterator OutputIt>
std::pair<InputIt, OutputIt> copy_bounded_unguarded(InputIt first,
                                                    Pred pred,
                                                    OutputIt dest_first,
                                                    OutputIt dest_last)
{
        while (pred(source(first)) && dest_first != dest_last) {
                copy_step(first, dest_first);
        }
        return std::make_pair(first, dest_first);
}

template <Iterator InputIt, OutputIterator OutputIt>
OutputIt copy_n(InputIt first,
                typename IteratorConcept<InputIt>::difference_type count,
                OutputIt dest_first)
{
        while (count) {
                sink(dest_first) = source(first);
                first = successor(first);
                dest_first = successor(dest_first);
                count = predecessor(count);
        }

        return dest_first;
}

/**
@requires Domain(Pred) = ValueType(InputIt)
@requires Domain(Fn) = ValueType(InputIt)
@requires CoDomain(Fn) = ValueType(OutputIt)
*/
template <Iterator InputIt,
          OutputIterator OutputIt,
          UnaryPredicate Pred,
          UnaryFunction Fn>
OutputIt apply_copy_if(
    InputIt first, InputIt last, OutputIt dest_first, Fn fn, Pred pred)
{
        while (first != last) {
                if (pred(source(first))) {
                        sink(dest_first) = fn(source(first));
                        dest_first = successor(dest_first);
                }
                first = successor(first);
        }

        return dest_first;
}

template <Iterator InputIt,
          OutputIterator OutputIt,
          UnaryFunction Fn,
          UnaryPredicate Pred>
std::pair<InputIt, OutputIt> apply_copy_bounded_if(InputIt first,
                                                   InputIt last,
                                                   OutputIt dest_first,
                                                   OutputIt dest_last,
                                                   Fn fn,
                                                   Pred pred)
{
        while (first != last && dest_first != dest_last) {
                if (pred(source(first))) {
                        sink(dest_first) = fn(source(first));
                        dest_first = successor(dest_first);
                }
                first = successor(first);
        }
        return std::make_pair(first, dest_first);
}

/**
@requires Domain(Fn) = ValueType(InputIt)
@requires CoDomain(Fn) = ValueType(OutputIt)
*/
template <Iterator InputIt, OutputIterator OutputIt, UnaryFunction Fn>
OutputIt apply_copy(InputIt first, InputIt last, OutputIt dest_first, Fn fn)
{
        while (first != last) {
                sink(dest_first) = fn(source(first));
                dest_first = successor(dest_first);
                first = successor(first);
        }

        return dest_first;
}

template <Iterator InputIt, OutputIterator OutputIt, UnaryFunction Fn>
OutputIt apply_copy_n(InputIt first,
                      typename IteratorConcept<InputIt>::difference_type count,
                      OutputIt dest_first,
                      Fn fn)
{
        while (count) {
                sink(dest_first) = fn(source(first));
                first = successor(first);
                dest_first = successor(dest_first);
                --count;
        }

        return dest_first;
}

// TODO(nicolas): remove std::pair
template <InputIterator I0, InputIterator I1>
std::pair<typename IteratorConcept<I0>::difference_type,
          typename IteratorConcept<I1>::difference_type>
find_mismatch_n_m(I0 f0,
                  typename IteratorConcept<I0>::difference_type n0,
                  I1 f1,
                  typename IteratorConcept<I1>::difference_type n1)
{
        while (n0 && n1 && *f0 == *f1) {
                --n0;
                --n1;
        }

        return std::make_pair(n0, n1);
}
}

// binary operations
namespace algos
{
// function pointers
template <typename T> struct BinaryOperationConcept<T &(*)(T &, T &)> {
        using type = T;
};
}

namespace algos
{
template <typename T> struct maximum {
        T &operator()(T &x, T &y) const { return max(x, y); }

        T const &operator()(T const &x, T const &y) const { return max(x, y); }
};

template <typename T> struct BinaryOperationConcept<maximum<T>> {
        using type = T;
};

template <Iterator I, BinaryOperation Op, UnaryFunction F>
typename BinaryOperationConcept<Op>::type
reduce_nonempty(I first, I last, Op op, F fun)
{
        typename BinaryOperationConcept<Op>::type result = fun(first);
        first = successor(first);
        while (first != last) {
                result = op(result, fun(first));
                first = successor(first);
        }

        return result;
}

template <Iterator I, BinaryOperation Op, UnaryFunction F>
typename BinaryOperationConcept<Op>::type
reduce(I first,
       I last,
       Op op,
       F fun,
       typename BinaryOperationConcept<Op>::type zero)
{
        if (first == last) {
                return zero;
        }

        return reduce_nonempty(first, last, op, fun);
}
}

// tree concepts and algorithms
namespace algos
{
#define TreeCoordinate typename

template <typename T> struct TreeCoordinateConcept {
        using WeightType = void;
};

template <TreeCoordinate C> C descendants_begin(C c);
template <TreeCoordinate C> C descendants_end(C c);
}

namespace algos
{
template <TreeCoordinate C>
typename TreeCoordinateConcept<C>::WeightType weight_recursive(C c)
{
        using N = typename TreeCoordinateConcept<C>::WeightType;
        N weight(0);

        auto first = descendants_begin(c);
        auto last = descendants_end(c);

        while (first != last) {
                weight = weight + weight_recursive(source(first));
                first = successor(first);
        }
        return successor(weight);
}

template <TreeCoordinate C>
typename TreeCoordinateConcept<C>::WeightType height_recursive(const C &c)
{
        using N = typename TreeCoordinateConcept<C>::WeightType;

        N height(0);

        auto first = descendants_begin(c);
        auto last = descendants_end(c);
        while (first != last) {
                auto branch_height = height_recursive(source(first));
                height = algos::max(height, branch_height);
                first = successor(first);
        }

        return successor(height);
}

enum VisitType { PRE, POST };
template <TreeCoordinate C, typename Proc>
Proc traverse_nonempty(const C &c, Proc proc)
{
        proc(PRE, c);

        auto first = descendants_begin(c);
        auto last = descendants_end(c);
        while (first != last) {
                proc = traverse_nonempty(source(first), proc);
                first = successor(first);
        }

        proc(POST, c);
        return proc;
}
}

namespace algos
{

template <TreeCoordinate C, typename Proc> struct visit_element {
        Proc proc;

        visit_element(Proc proc) : proc(proc) {}
        visit_element(visit_element const &) = default;
        visit_element &operator=(visit_element const &) = default;

        void operator()(VisitType visit, C c)
        {
                if (visit != PRE)
                        return;
                proc(source(c));
        }
};

template <TreeCoordinate C, typename Proc>
void traverse_each(C const &c, Proc proc)
{
        traverse_nonempty(c, visit_element<C, Proc>(proc));
}
};