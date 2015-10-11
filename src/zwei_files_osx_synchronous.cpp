#include "block_allocator.hpp"

#include <algorithm>
#include <cmath>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct FileLoaderFileEntry {
        char const *path;
        size_t size;
        uint32_t tag;
        bool load_error;
        bool accepted;
};

struct FileLoader {
        MemoryArena arena;
        MemoryArena transient_arena;
        size_t entries_capacity;
        size_t entries_count;
        FileLoaderFileEntry *entries;
        // organized so that error files come first, then available files, then
        // pending
        FileLoaderHandle *file_handles;
        FileLoaderHandle *last_erroneous_file;
        FileLoaderHandle *last_available_file;
        FileLoaderContent *file_contents;
        size_t loaded_files_count;
        size_t file_memory_size;
        MemoryBlockAllocator files_memory_allocator;
};

template <typename N, Integral I> N greatest_multiple(N x, I divider)
{
        return divider * I(std::floor(double(x) / divider));
}

size_t get_file_loader_allocation_size(size_t maximum_file_count,
                                       size_t maximum_file_size)
{
        return sizeof(FileLoader) +
               maximum_file_count *
                   (sizeof(FileLoaderFileEntry) + sizeof(FileLoaderHandle) +
                    sizeof(FileLoaderContent)) +
               maximum_file_size;
}

FileLoader &
create_file_loader(size_t maximum_file_count, void *memory, size_t memory_size)
{
        zw_assert(sizeof(FileLoader) < memory_size, "not enough memory");
        MemoryArena base = memory_arena(memory, memory_size);
        FileLoader *ptr =
            new (push_bytes(&base, sizeof(FileLoader))) FileLoader;
        FileLoader &file_loader = *ptr;
        file_loader.arena = push_sub_arena(base, memory_size - base.used);
        file_loader.transient_arena =
            push_sub_arena(file_loader.arena, MEGABYTES(1));
        file_loader.entries_capacity = maximum_file_count;
        file_loader.entries =
            push_array_rvalue(&file_loader.arena, file_loader.entries,
                              file_loader.entries_capacity);
        file_loader.file_handles =
            push_array_rvalue(&file_loader.arena, file_loader.file_handles,
                              file_loader.entries_capacity);
        file_loader.last_erroneous_file = file_loader.file_handles;
        file_loader.last_available_file = file_loader.file_handles;
        file_loader.file_contents =
            push_array_rvalue(&file_loader.arena, file_loader.file_contents,
                              file_loader.entries_capacity);
        file_loader.loaded_files_count = 0;
        size_t file_memory_size =
            greatest_multiple(file_loader.arena.size - file_loader.arena.used,
                              MemoryBlockListHeader::MINIMUM_SIZE);
        file_loader.file_memory_size = file_memory_size;
        initialize(file_loader.files_memory_allocator,
                   push_bytes(&file_loader.arena, file_memory_size),
                   file_memory_size);

        return file_loader;
}

void destroy(FileLoader &file_loader) { file_loader.~FileLoader(); }

zw_internal FileLoaderFileEntry &push_file_entry(FileLoader &file_loader,
                                                 char const *filepath)
{
        size_t entry_index = file_loader.entries_count;
        zw_assert(entry_index <= file_loader.entries_capacity,
                  "too many files");
        // TODO(nicolas): what should we do when we reach max capacity?

        FileLoaderFileEntry &entry = file_loader.entries[entry_index];
        entry = {};
        entry.path = filepath;
        entry.size = 0;

        file_loader.file_handles[entry_index].id = uint32_t(entry_index);
        file_loader.file_contents[entry_index] = {};
        ++file_loader.entries_count;

        return entry;
}

void push_file(FileLoader &file_loader,
               char const *filepath,
               size_t expected_filesize,
               uint32_t tag)
{
        auto &entry = push_file_entry(file_loader, filepath);
        entry.size = expected_filesize;
        entry.tag = tag;
}

void push_file(FileLoader &file_loader, char const *filepath, uint32_t tag)
{
        auto &entry = push_file_entry(file_loader, filepath);
        entry.size = 0; // NOTE(nicolas): query size at open
        entry.tag = tag;
}

size_t count_pending_files(FileLoader &file_loader)
{
        return file_loader.entries_count - file_loader.loaded_files_count;
}

zw_internal uint8_t *read_entire_file(FileLoader &file_loader,
                                      FileLoaderFileEntry &entry)
{
        char const *filepath = entry.path;

        SPDR_SCOPE2(global_spdr, "file_loader", "read_entire_file",
                    SPDR_STR("path", filepath),
                    SPDR_INT("size", int(entry.size)));

        SPDR_BEGIN(global_spdr, "file_loader", "open");
        int entry_fd = open(filepath, O_RDONLY, 0);
        if (entry_fd < 0 && errno == EACCES) {
                entry.load_error = true;
                return nullptr;
        }
        DEFER(close(entry_fd));

#if ZWEI_DISABLED
        fcntl(entry_fd, F_NOCACHE, 1); // we only process the files once
#endif
        fcntl(entry_fd, F_RDAHEAD, 1);

        SPDR_END(global_spdr, "file_loader", "open");

        // @id e67985425c3f2b244c1375091a0b1e0b7f2510f2
        if (entry.size == 0) {
                SPDR_SCOPE(global_spdr, "file_loader", "stat");
                struct stat filestats;
                if (0 != fstat(entry_fd, &filestats)) {
                        entry.load_error = true;
                        return nullptr;
                }
                if (!(filestats.st_mode & S_IFREG)) {
                        zw_assert(false, "ignoring non file");
                        entry.load_error = true;
                        return nullptr;
                }
                entry.size = filestats.st_size;
        }

        if (entry.size > file_loader.file_memory_size) {
                // NOTE(nicolas): this entry will never get loaded,
                // it's too large for our buffers
                entry.load_error = true;
                return nullptr;
        }

        size_t filesize = entry.size;
        uint8_t *d_bytes =
            (uint8_t *)allocate(file_loader.files_memory_allocator, entry.size);
        if (!d_bytes) {
                // NOTE(nicolas): we could not allocate enough
                // memory. It's probably because we have enough data
                // pending that we have to wait for processing going on
                // on the user side that we can't find free blocks.
                //
                // So we have to wait a bit and relinquish control to
                // them.
                SPDR_EVENT(global_spdr, "file_loader", "oom");
                return nullptr;
        }

        if (entry.size == 0) {
                // empty content
                return d_bytes;
        }

        auto const d_bytes_start = d_bytes;
        auto d_bytes_last = d_bytes + filesize;

        while (d_bytes != d_bytes_last) {
                size_t size_to_read = d_bytes_last - d_bytes;

                SPDR_BEGIN1(global_spdr, "io", "read",
                            SPDR_INT("size", int(size_to_read)));
                ssize_t size_read = read(entry_fd, d_bytes, size_to_read);
                SPDR_END(global_spdr, "io", "read");

                if (size_read == 0) {
                        break;
                } else if (size_read < 0) {
                        zw_assert(false, "error");
                        entry.load_error = true;
                        break;
                }
                d_bytes = d_bytes + size_read;
        }

        if (entry.load_error) {
                free(file_loader.files_memory_allocator, d_bytes);
        }

        return d_bytes_start;
}

void wait_for_available_files(FileLoader &file_loader)
{
        SPDR_SCOPE(global_spdr, "file_loader", __FUNCTION__);
        auto entry_index = file_loader.loaded_files_count;
        MemoryArena const original_arena = file_loader.arena;
        bool success = false;
        while (!success && entry_index < file_loader.entries_count) {
                auto &entry = file_loader.entries[entry_index];
                auto start = read_entire_file(file_loader, entry);
                if (entry.load_error) {
                        file_loader.arena = original_arena;
                        ++entry_index;
                        continue; // try another one
                } else if (!start) {
                        break;
                }

                auto &content = file_loader.file_contents[entry_index];
                content.range.first = start;
                content.range.second = content.range.first + entry.size;
                success = true;
                ++entry_index;
        }
        file_loader.loaded_files_count = entry_index;

        {
                // NOTE(nicolas): partition the file handles into categories
                using algos::begin;
                using algos::end;

                auto first_handle = file_loader.file_handles;
                auto last_handle =
                    file_loader.file_handles + file_loader.loaded_files_count;

                auto with_load_error =
                    [&file_loader](FileLoaderHandle const &x) {
                            auto entry_index = x.id;
                            return file_loader.entries[entry_index].load_error;
                    };

                auto is_available = [&file_loader](FileLoaderHandle const &x) {
                        auto entry_index = x.id;
                        if (file_loader.entries[entry_index].accepted) {
                                return false;
                        }
                        auto const &content =
                            file_loader.file_contents[entry_index];
                        return begin(content) != nullptr &&
                               end(content) != nullptr;
                };

                auto last_error =
                    std::partition(first_handle, last_handle, with_load_error);

                auto last_available =
                    std::partition(last_error, last_handle, is_available);

                file_loader.last_erroneous_file = last_error;
                file_loader.last_available_file = last_available;
        }
        SPDR_COUNTER2(global_spdr, "file_loader", "arena",
                      SPDR_INT("success", int(success)),
                      SPDR_INT("used", int(file_loader.arena.used)));
}

FileLoaderHandleList available_files(FileLoader &file_loader)
{
        return {
            {file_loader.last_erroneous_file, file_loader.last_available_file}};
}

FileLoaderHandleList load_error_files(FileLoader &file_loader)
{
        return {{file_loader.file_handles, file_loader.last_erroneous_file}};
}

void accept(FileLoader &file_loader, FileLoaderHandle const &file_handle)
{
        zw_assert(!file_loader.entries[file_handle.id].accepted,
                  "already accepted");
        zw_assert(begin(file_loader.file_contents[file_handle.id]),
                  "no content");
        file_loader.entries[file_handle.id].accepted = true;
}

void release_content(FileLoader &file_loader,
                     FileLoaderHandle const &file_handle)
{
        SPDR_SCOPE(global_spdr, "file_loader", __FUNCTION__);
        size_t entry_index = file_handle.id;
        auto &content = file_loader.file_contents[entry_index];

        zw_assert(begin(content), "uh?");
        free(file_loader.files_memory_allocator, (void *)begin(content));
        content.range.first = nullptr;
        content.range.second = nullptr;

        SPDR_COUNTER1(global_spdr, "file_loader", "arena",
                      SPDR_INT("used", int(file_loader.arena.used)));
}

FileLoaderContent get_content(FileLoader &file_loader,
                              FileLoaderHandle const &file_handle)
{
        zw_assert(file_loader.entries[file_handle.id].accepted,
                  "you must accept a file beforehand");
        return file_loader.file_contents[file_handle.id];
}

uint32_t get_tag(FileLoader &file_loader, FileLoaderHandle const &file_handle)
{
        return file_loader.entries[file_handle.id].tag;
}

char const *get_filepath(FileLoader &file_loader,
                         FileLoaderHandle const &file_handle)
{
        return file_loader.entries[file_handle.id].path;
}

char const *get_filename(FileLoader &file_loader,
                         FileLoaderHandle const &file_handle)
{
        // @id 206be8ebf9339bd5062d0b719eb50ef771a48afa
        char const *path = get_filepath(file_loader, file_handle);
        char const *pos = algos::find_last_unguarded(
            path, [](char const &x) { return x; }, '/');
        if (*pos == '/') {
                pos = pos + 1;
        }

        return pos;
}
