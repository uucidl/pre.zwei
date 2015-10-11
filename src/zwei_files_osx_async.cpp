/**
   Implementation of zwei_files.hpp using libdispatch.

   We queue files for loading and only receive their results at an
   indeterminate time.
*/

#include "block_allocator.hpp"

#include <atomic>
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
        size_t load_skipped_count;
        size_t load_started_count;

        FileLoaderHandle *available;
        size_t available_capacity;
        size_t available_count;

        FileLoaderHandle *errors;
        size_t errors_capacity;
        size_t errors_count;

        size_t finished_count;

        std::atomic_int available_since_wait;

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

        file_loader.entries =
            push_array_rvalue(&arena, file_loader.entries, maximum_file_count);
        file_loader.entries_capacity = maximum_file_count;
        file_loader.entries_count = 0;
        file_loader.load_skipped_count = 0;
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

        std::atomic_init(&file_loader.available_since_wait, 0);

        // TODO(nicolas: @leak the created queue must be released
        file_loader.task_queue =
            dispatch_queue_create("com.uucidl.shasum", DISPATCH_QUEUE_SERIAL);
        dispatch_set_target_queue(
            file_loader.task_queue,
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0));

        // TODO(nicolas): @leak the semaphore must be released
        file_loader.data_available_semaphore = dispatch_semaphore_create(0);

        size_t available_memory_size = arena.size - arena.used;

        new (&file_loader.content_allocator) MemoryBlockAllocator();
        initialize(file_loader.content_allocator,
                   push_bytes(&arena, available_memory_size),
                   available_memory_size);

        return file_loader;
}

void destroy(FileLoader &file_loader) { file_loader.~FileLoader(); }

void push_file(FileLoader &file_loader,
               char const *filepath,
               size_t expected_filesize,
               uint32_t tag)
{
        zw_assert(file_loader.entries_count < file_loader.entries_capacity,
                  "over");
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

        /*

          Are there any pending file? If no then we're done
          Otherwise try allocating result objects.

          If cannot, then it means people are still processing our files, and we
          can
          only bail out to the caller.

          If we can we can spawn immediately a read onto the file and
          wait for the first available file.
         */

        // TODO(nicolas): kind of bizarre to have to do this here.
        if (count_pending_files(file_loader) == 0)
                return;

        auto const first_entry = file_loader.entries;
        auto const last_entry = file_loader.entries + file_loader.entries_count;
        auto current_entry = first_entry;

        auto needs_load_count = [](FileLoader &fl) {
                return fl.entries_count - fl.load_skipped_count -
                       fl.load_started_count;
        };

        SPDR_BEGIN(global_spdr, "file_loader", "submitting");
        while (current_entry != last_entry &&
               needs_load_count(file_loader) > 0) {
                // TODO(nicolas): track pending concurrent read size count.. we
                // don't need
                // to submit too many

                auto first_pending = algos::find_if(
                    current_entry, last_entry,
                    [](FileLoaderEntry const &entry) {
                            return entry.state == FileLoaderEntry::PENDING;
                    });

                zw_assert(first_pending != last_entry, "uh?");

                auto &entry = algos::source(first_pending);
                auto *entry_ptr = const_cast<FileLoaderEntry *>(&entry);

                int entry_index = int(entry_ptr - first_entry);
                // TODO(nicolas): don't submit all opens so early, it
                // seems that the OS will in the end perform the reads
                // in chunks in parallel, which means seeking. At
                // least in my tests the async version is slower than
                // the sync version!
                bool submit_load = false;
                int entry_fd = open(entry.path, O_RDONLY | O_NONBLOCK, 0);
                if (entry_fd < 0) {
                        if (errno == EMFILE) {
                                // We reached the maximum number of
                                // opened file descriptors. Let's
                                // pause a bit.
                                SPDR_EVENT(global_spdr, "file_loader",
                                           "too-many-files");
                                break;
                        } else if (errno == EACCES || errno == ENOENT) {
                                // access denied
                                entry.state = FileLoaderEntry::ERROR;
                                ++file_loader.load_skipped_count;
                        } else {
                                SPDR_EVENT(global_spdr, "file_loader",
                                           "unexpected error");
                                // try later, maybe it's a temporary error due
                                // to lack of resources

                                // TODO(nicolas): actually check that out (the
                                // errno would tell us if we reached the fd
                                // limit for instance)
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
                                        ++file_loader.load_skipped_count;
                                } else if (!(filestats.st_mode & S_IFREG)) {
                                        entry.state = FileLoaderEntry::ERROR;
                                        ++file_loader.load_skipped_count;
                                } else {
                                        entry.size = filestats.st_size;
                                        submit_load = true;
                                }
                        } else {
                                submit_load = true;
                        }
                }

                if (!submit_load) {
                        close(entry_fd);
                } else {
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
                                  // TODO(nicolas): @thread_unsafe we
                                  // probably need a memory barrier here,
                                  // to ensure all writes made to the
                                  // entry are seen before we change the
                                  // status.
                                  entry.state = FileLoaderEntry::AVAILABLE;
                                  if (!std::atomic_fetch_add(
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
                current_entry = algos::successor(first_pending);
        }
        SPDR_END(global_spdr, "file_loader", "submitting");

        file_loader_update_counters(file_loader);

        auto new_available_count =
            std::atomic_exchange(&file_loader.available_since_wait, 0);
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
