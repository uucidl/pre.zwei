/**
   Implementation of zwei_files.hpp using libdispatch.

   We queue files for loading and only receive their results at an
   indeterminate time.
*/

#include "block_allocator.hpp"
#include "zwei_concurrency.hpp"

#include <cerrno>
#include <sys/stat.h>

struct FileLoaderEntry {
        char const *path;
        size_t size;
        uint32_t tag;
        enum { PENDING, LOADING, AVAILABLE, USER, RELEASED, ERROR } state;
        int fd;
        void *content;
        size_t content_size;
};

struct FileLoader {
        FileLoaderEntry *entries;
        size_t entries_capacity;
        size_t entries_count;
        size_t load_started_count;

        FileLoaderHandle *available;
        size_t available_capacity;
        size_t available_count;

        FileLoaderHandle *errors;
        size_t errors_capacity;
        size_t errors_count;

        size_t finished_count;

        uint32_t available_since_wait;

        MemoryBlockAllocator content_allocator;

        dispatch_queue_t task_queue;
        dispatch_semaphore_t data_available_semaphore;
};

/** @return the minimum needed size for allocating a file loader */
size_t get_file_loader_allocation_size(size_t maximum_file_count,
                                       size_t maximum_file_size)
{
        (void)maximum_file_size;
        return sizeof(FileLoader) +
               maximum_file_count * sizeof(FileLoaderEntry) +
               maximum_file_count * 2 * sizeof(FileLoaderHandle) +
               get_block_allocator_arena_size(maximum_file_size);
}

FileLoader &
create_file_loader(size_t maximum_file_count, void *memory, size_t memory_size)
{
        MemoryArena arena = memory_arena(memory, memory_size);
        FileLoader *ptr = push_pointer_rvalue(&arena, ptr);
        FileLoader &file_loader = *ptr;
        new (&file_loader) FileLoader;

        file_loader.entries =
            push_array_rvalue(&arena, file_loader.entries, maximum_file_count);
        file_loader.entries_capacity = maximum_file_count;
        file_loader.entries_count = 0;
        file_loader.load_started_count = 0;

        file_loader.available = push_array_rvalue(&arena, file_loader.available,
                                                  maximum_file_count);
        file_loader.available_capacity = maximum_file_count;
        file_loader.available_count = 0;

        file_loader.errors =
            push_array_rvalue(&arena, file_loader.errors, maximum_file_count);
        file_loader.errors_capacity = maximum_file_count;
        file_loader.errors_count = 0;

        file_loader.finished_count = 0;

        file_loader.task_queue =
            dispatch_queue_create("com.uucidl.files", DISPATCH_QUEUE_SERIAL);
        dispatch_set_target_queue(
            file_loader.task_queue,
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0));

        file_loader.data_available_semaphore = dispatch_semaphore_create(0);

        size_t available_memory_size = arena.size - arena.used;

        initialize(file_loader.content_allocator,
                   push_bytes(&arena, available_memory_size),
                   available_memory_size);

        return file_loader;
}

void destroy(FileLoader &file_loader)
{
        dispatch_release(file_loader.task_queue);
        dispatch_release(file_loader.data_available_semaphore);
        file_loader.~FileLoader();
}

void push_file(FileLoader &file_loader,
               char const *filepath,
               size_t expected_filesize,
               uint32_t tag)
{
        zw_assert(file_loader.entries_count < file_loader.entries_capacity,
                  "over");
        if (file_loader.entries_count >= file_loader.entries_capacity) {
                return;
        }
        FileLoaderEntry &entry = file_loader.entries[file_loader.entries_count];
        entry.path = filepath;
        entry.size = expected_filesize;
        entry.tag = tag;
        entry.state = FileLoaderEntry::PENDING;
        ++file_loader.entries_count;
}

void push_file(FileLoader &file_loader, char const *filepath, uint32_t tag)
{
        push_file(file_loader, filepath, 0, tag);
}

size_t count_pending_files(FileLoader &file_loader)
{
        return file_loader.entries_count - file_loader.finished_count;
}

namespace
{
void file_loader_update_counters(FileLoader &file_loader)
{
        auto const first_entry = file_loader.entries;
        auto const last_entry = first_entry + file_loader.entries_count;

        size_t other_finished_count = 0;

        file_loader.available_count = 0;
        auto last_available = algos::apply_copy_if(
            first_entry, last_entry, file_loader.available,
            [first_entry](FileLoaderEntry const &entry) -> FileLoaderHandle {
                    return {uint32_t(&entry - first_entry)};
            },
            [&other_finished_count](FileLoaderEntry const &entry) {
                    if (entry.state == FileLoaderEntry::AVAILABLE) {
                            return true;
                    } else if (entry.state > FileLoaderEntry::AVAILABLE) {
                            ++other_finished_count;
                    }
                    return false;
            });
        file_loader.available_count = last_available - file_loader.available;
        file_loader.finished_count = other_finished_count;

        SPDR_COUNTER1(global_spdr, "file_loader", "available_count",
                      SPDR_INT("value", int(file_loader.available_count)));
        SPDR_COUNTER1(global_spdr, "file_loader", "pending_count",
                      SPDR_INT("value", int(file_loader.entries_count -
                                            file_loader.available_count -
                                            file_loader.finished_count)));
}
}

void wait_for_available_files(FileLoader &file_loader)
{
        SPDR_SCOPE(global_spdr, "file_loader", __FUNCTION__);

        if (count_pending_files(file_loader) == 0)
                return;

        SPDR_BEGIN(global_spdr, "file_loader", "submitting");

        auto next_pending_entry = [](FileLoaderEntry *first,
                                     FileLoaderEntry *last) {
                return algos::find_if(
                    first, last, [](FileLoaderEntry const &entry) {
                            return entry.state == FileLoaderEntry::PENDING;
                    });
        };

        auto const entries_last =
            file_loader.entries + file_loader.entries_count;
        auto const entries_first = file_loader.entries;
        auto candidates_first = entries_first;
        FileLoaderEntry *entry_pos;
        while ((entry_pos = next_pending_entry(candidates_first,
                                               entries_last)) != entries_last) {
                candidates_first = algos::successor(entry_pos);

                auto &entry = algos::source(entry_pos);
                int entry_fd = open(entry.path, O_RDONLY | O_NONBLOCK, 0);
                if (entry_fd < 0) {
                        if (errno == EACCES || errno == ENOENT) {
                                // access denied
                                entry.state = FileLoaderEntry::ERROR;
                        } else if (errno == EMFILE) {
                                // We reached the maximum number of
                                // opened file descriptors. Let's
                                // pause a bit.
                                SPDR_EVENT(global_spdr, "file_loader",
                                           "too-many-files");
                                break;
                        } else {
                                SPDR_EVENT(global_spdr, "file_loader",
                                           "unexpected error");
                                // try later, maybe it's a temporary error due
                                // to lack of resources
                                zw_assert(false, "skipping this iteration");
                                break;
                        }
                } else {
                        fcntl(entry_fd, F_RDAHEAD, 1);

                        // @copypaste e67985425c3f2b244c1375091a0b1e0b7f2510f2
                        if (entry.size == 0) {
                                struct stat filestats;
                                if (0 != fstat(entry_fd, &filestats)) {
                                        entry.state = FileLoaderEntry::ERROR;
                                } else if (!(filestats.st_mode & S_IFREG)) {
                                        entry.state = FileLoaderEntry::ERROR;
                                } else {
                                        entry.size = filestats.st_size;
                                }
                        }
                }

                if (entry.state == FileLoaderEntry::ERROR) {
                        close(entry_fd);
                } else {
                        int entry_index = int(entry_pos - entries_first);
                        SPDR_ASYNC_EVENT_BEGIN1(
                            global_spdr, "file_loader", "load", entry_index,
                            SPDR_INT("size", int(entry.size)));
                        auto content =
                            allocate(file_loader.content_allocator, entry.size);
                        if (!content) {
                                close(entry_fd);
                                // NOTE(nicolas): we've allocated too
                                // much, no need to spawn new tasks
                                // for now.
                                break;
                        }

                        entry.fd = entry_fd;
                        entry.content = content;
                        entry.content_size = entry.size;
                        void (^read_all)() = ^() {
                          SPDR_SCOPE1(global_spdr, "file_loader",
                                      "entry_read_all",
                                      SPDR_INT("size", int(entry.size)));
                          auto total_read =
                              read(entry.fd, entry.content, entry.size);
                          if ((size_t)total_read != entry.size) {
                                  zw_assert(total_read == -1,
                                            "we want one shot read");
                                  perror("something occured: ");
                                  entry.state = FileLoaderEntry::ERROR;
                          } else {
                                  entry.state = FileLoaderEntry::AVAILABLE;

                                  COMPLETE_PREVIOUS_WRITES_BEFORE_FUTURE_WRITES;
                                  if (!atomic_add_u32(
                                          &file_loader.available_since_wait,
                                          1)) {
                                          dispatch_semaphore_signal(
                                              file_loader
                                                  .data_available_semaphore);
                                  }
                          }
                          close(entry.fd);
                          SPDR_ASYNC_EVENT_END(global_spdr, "file_loader",
                                               "load", entry_index);
                        };

                        entry.state = FileLoaderEntry::LOADING;
                        ++file_loader.load_started_count;
                        dispatch_async(file_loader.task_queue, read_all);
                }
        }
        SPDR_END(global_spdr, "file_loader", "submitting");

        file_loader_update_counters(file_loader);

        auto new_available_count =
            atomic_exchange_u32(&file_loader.available_since_wait, 0);
        if (!new_available_count &&
            count_pending_files(file_loader) - file_loader.available_count >
                0) {
                SPDR_BEGIN(global_spdr, "file_loader", "waiting_for_available");
                dispatch_semaphore_wait(file_loader.data_available_semaphore,
                                        DISPATCH_TIME_FOREVER);
                SPDR_END(global_spdr, "file_loader", "waiting_for_available");
        }
}

FileLoaderHandleList available_files(FileLoader &file_loader)
{
        file_loader_update_counters(file_loader);
        return {{file_loader.available,
                 file_loader.available + file_loader.available_count}};
}

FileLoaderHandleList load_error_files(FileLoader &file_loader)
{
        auto const first_entry = file_loader.entries;
        auto const last_entry = first_entry + file_loader.entries_count;

        size_t other_finished_count = 0;

        auto last_error = algos::apply_copy_if(
            first_entry, last_entry, file_loader.errors,
            [first_entry](FileLoaderEntry const &entry) -> FileLoaderHandle {
                    return {uint32_t(&entry - first_entry)};
            },
            [&other_finished_count](FileLoaderEntry const &entry) {
                    return entry.state == FileLoaderEntry::ERROR;
            });
        file_loader.errors_count = last_error - file_loader.errors;

        return {{file_loader.errors, last_error}};
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
        // @copypaste 206be8ebf9339bd5062d0b719eb50ef771a48afa
        char const *path = get_filepath(file_loader, file_handle);
        // TODO(nicolas): this really belongs to a OSX/Unix path library fn
        char const *pos = algos::find_last_unguarded(
            path, [](char const &x) { return x; }, '/');
        if (*pos == '/') {
                pos = pos + 1;
        }

        return pos;
}

void accept(FileLoader &file_loader, FileLoaderHandle const &file_handle)
{
        auto &entry = file_loader.entries[file_handle.id];
        zw_assert(entry.state == FileLoaderEntry::AVAILABLE, "invalid state");
        entry.state = FileLoaderEntry::USER;
}

FileLoaderContent get_content(FileLoader &file_loader,
                              FileLoaderHandle const &file_handle)
{
        auto const &entry = file_loader.entries[file_handle.id];
        zw_assert(entry.state == FileLoaderEntry::USER,
                  "you must accept a file");
        auto first_byte = (uint8_t const *)entry.content;
        auto last_byte = first_byte + entry.content_size;
        return {{first_byte, last_byte}};
}

void release_content(FileLoader &file_loader,
                     FileLoaderHandle const &file_handle)
{
        auto &entry = file_loader.entries[file_handle.id];
        zw_assert(entry.state == FileLoaderEntry::USER, "invalid state");
        entry.state = FileLoaderEntry::RELEASED;
        free(file_loader.content_allocator, entry.content);
        entry.content = nullptr;
        entry.content_size = 0;
}
