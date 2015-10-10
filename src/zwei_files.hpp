#pragma once

/**
   @file
   Mass File Loading

   Prototyping future shape of file processing API

   Requirements:
   - maximum I/O performance,
   - managing memory for every file is not a concern for the user,
   - we can apply one processing to every file,
   - we can allocate some additional memory buffer for processing, proportional
   to the file,
   - file processing can be done in parallel

   Optional:
   - cancelling?

   TODO(nicolas): considering auto accepting when fetching available_files,
   it seems like a typical error to make not to accept them.
*/

#include <cstdint>
#include <utility>

struct FileLoaderContent {
        std::pair<uint8_t const *, uint8_t const *> range;

        friend uint8_t const *begin(FileLoaderContent const &x)
        {
                return x.range.first;
        }

        friend uint8_t const *end(FileLoaderContent const &x)
        {
                return x.range.second;
        }
};

struct FileLoaderHandle {
        uint32_t id;
};

struct FileLoaderHandleList {
        // TODO(nicolas): extract bounded range
        std::pair<FileLoaderHandle *, FileLoaderHandle *> range;

        friend FileLoaderHandle *begin(FileLoaderHandleList const &x)
        {
                return x.range.first;
        }

        friend FileLoaderHandle *end(FileLoaderHandleList const &x)
        {
                return x.range.second;
        }
};

/**
   - storage for all files which have to be loaded
   - storage for files currently loaded in memory, plus buffers
   - general purpose allocation structures: free lists etc..
   - platform specific resources that are performing the loading
*/
struct FileLoader;

/** @return the minimum needed size for allocating a file loader */
size_t get_file_loader_allocation_size(size_t maximum_file_count,
                                       size_t maximum_file_size);
FileLoader &
create_file_loader(size_t maximum_file_count, void *memory, size_t memory_size);
void destroy(FileLoader &file_loader);

void push_file(FileLoader &file_loader,
               char const *filepath,
               size_t expected_filesize,
               uint32_t tag);
void push_file(FileLoader &file_loader, char const *filepath, uint32_t tag);

size_t count_pending_files(FileLoader &file_loader);
void wait_for_available_files(FileLoader &file_loader);

FileLoaderHandleList available_files(FileLoader &file_loader);
FileLoaderHandleList load_error_files(FileLoader &file_loader);

uint32_t get_tag(FileLoader &file_loader, FileLoaderHandle const &file_handle);
char const *get_filepath(FileLoader &file_loader,
                         FileLoaderHandle const &file_handle);
char const *get_filename(FileLoader &file_loader,
                         FileLoaderHandle const &file_handle);

void accept(FileLoader &file_loader, FileLoaderHandle const &file_handle);
FileLoaderContent get_content(FileLoader &file_loader,
                              FileLoaderHandle const &file_handle);
void release_content(FileLoader &file_loader,
                     FileLoaderHandle const &file_handle);
