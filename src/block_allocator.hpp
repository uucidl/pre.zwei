#pragma once
// An allocator dividing a region of memory in blocks that can be split and
// joined as needed

#include "algos.hpp"
#include "algos_concepts.hpp"
#include "zwei_app.hpp"
#include "zwei_inlines.hpp"

#include "../modules/uu.spdr/include/spdr/spdr.hh"

#include <cmath> // for std::ceil
#include <mutex> // for std::mutex

template <typename N, Integral I> N smallest_multiple(N x, I divider)
{
        return divider * I(std::ceil(double(x) / divider));
}

struct BlockListHeader {
        enum { MINIMUM_SIZE = 4096 };
        BlockListHeader *next;
        size_t total_size;
};

struct BlockAllocator {
        uint8_t *base;
        std::mutex free_list_mutex;
        BlockListHeader free_list_sentinel;
};

size_t block_allocator_get_arena_size(size_t maximum_allocation_size)
{
        return smallest_multiple(maximum_allocation_size +
                                     sizeof(BlockListHeader),
                                 BlockListHeader::MINIMUM_SIZE);
}

// NOTE(nicolas): a block allocator is neither copyable nor moveable
void block_allocator_initialize(BlockAllocator *allocator_ptr,
                                void *memory,
                                size_t memory_size)
{
        auto &allocator = *allocator_ptr;
        zw_assert(memory_size > sizeof(BlockListHeader), "not enough memory");
        BlockListHeader *all_memory = (BlockListHeader *)memory;
        {
                all_memory->total_size = memory_size;
                all_memory->next = &allocator.free_list_sentinel;
        }

        allocator.base = static_cast<uint8_t *>(memory);
        allocator.free_list_sentinel.next = all_memory;
}

template <> struct algos::IteratorConcept<BlockListHeader *> {
        using difference_type = ssize_t;
};

zw_internal BlockListHeader *successor(BlockListHeader *x) { return x->next; }

zw_internal void set_successor(BlockListHeader *x, BlockListHeader *x_next)
{
        x->next = x_next;
}

zw_internal void *memory(BlockListHeader *header)
{
        return (void *)(header + 1);
}

zw_internal size_t memory_size(BlockListHeader *header)
{
        return header->total_size - sizeof(*header);
}

zw_internal BlockListHeader *memory_header(void *memory)
{
        return ((BlockListHeader *)memory) - 1;
}

template <typename R> std::unique_lock<R> make_unique_lock(R &r)
{
        return std::move(std::unique_lock<R>(r));
}

zw_internal BlockListHeader *allocate_block(BlockAllocator &allocator,
                                            size_t content_size)
{
        auto free_list_lock = make_unique_lock(allocator.free_list_mutex);
        SPDR_BEGIN1(global_spdr, "allocator", "free_list::find_free_block",
                    SPDR_INT("content_size", int(content_size)));
        auto *sentinel = &allocator.free_list_sentinel;
        auto big_enough_prev = sentinel;
        auto big_enough = algos::find_if(
            successor(sentinel), sentinel,
            [&big_enough_prev, content_size](BlockListHeader &header) {
                    if (content_size > memory_size(&header)) {
                            big_enough_prev = &header;
                            return false;
                    }
                    return true;
            });
        SPDR_END(global_spdr, "allocator", "free_list::find_free_block");

        zw_assert(big_enough != sentinel, "overallocating");
        if (big_enough == sentinel) {
                return nullptr;
        }

        size_t needed_block_size =
            smallest_multiple(content_size + sizeof(BlockListHeader),
                              BlockListHeader::MINIMUM_SIZE);
        if (big_enough->total_size > needed_block_size) {
                SPDR_EVENT1(global_spdr, "allocator", "free_list::split_block",
                            SPDR_INT("size", int(needed_block_size)));
                // Split block as made necessary by the content size
                BlockListHeader *remainder_block =
                    (BlockListHeader *)((uint8_t *)big_enough +
                                        needed_block_size);
                remainder_block->total_size =
                    big_enough->total_size - needed_block_size;
                big_enough->total_size = needed_block_size;
                set_successor(big_enough_prev, remainder_block);
                set_successor(remainder_block, successor(big_enough));
        } else {
                SPDR_EVENT1(global_spdr, "allocator", "free_list::remove_block",
                            SPDR_INT("size", int(needed_block_size)));
                set_successor(big_enough_prev, successor(big_enough));
        }

        // not in any list anymore
        set_successor(big_enough, nullptr);

        return big_enough;
}

zw_internal void free_block(BlockAllocator &allocator, BlockListHeader *block)
{
        auto free_list_lock = make_unique_lock(allocator.free_list_mutex);
        BlockListHeader *free_blocks_sentinel = &allocator.free_list_sentinel;

        SPDR_BEGIN1(global_spdr, "allocator", "free_list::find_adjacent_block",
                    SPDR_INT("offset", int((uint8_t *)block - allocator.base)));
        auto adjacent_prev = free_blocks_sentinel;
        auto adjacent = algos::find_if(
            successor(free_blocks_sentinel), free_blocks_sentinel,
            [&adjacent_prev, block](BlockListHeader const &x) {
                    uint8_t *block_byte = (uint8_t *)block;
                    uint8_t *x_byte = (uint8_t *)&x;
                    bool adjacent =
                        (block_byte + block->total_size == x_byte) ||
                        (x_byte + x.total_size) == block_byte;
                    if (!adjacent) {
                            adjacent_prev = (BlockListHeader *)&x;
                            return false;
                    }
                    return true;
            });
        SPDR_END(global_spdr, "allocator", "free_list::find_adjacent_block");

        if (adjacent != free_blocks_sentinel) {
                SPDR_EVENT1(global_spdr, "allocator", "free_list::merge_block",
                            SPDR_INT("size", int(block->total_size)));
                if (block > adjacent) {
                        adjacent->total_size += block->total_size;
                } else {
                        block->total_size += adjacent->total_size;
                        set_successor(adjacent_prev, block);
                        set_successor(block, successor(adjacent));
                }
        } else {
                SPDR_EVENT1(global_spdr, "allocator", "free_list::add_block",
                            SPDR_INT("size", int(block->total_size)));
                auto first = successor(free_blocks_sentinel);

                set_successor(free_blocks_sentinel, block);
                set_successor(block, first);
        }
}

void *allocate(BlockAllocator &allocator, size_t size)
{
        auto header = allocate_block(allocator, size);
        if (!header) {
                return nullptr;
        }
        return memory(header);
}

void free(BlockAllocator &allocator, void *pointer)
{
        auto header = memory_header(pointer);
        free_block(allocator, header);
}
