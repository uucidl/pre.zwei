#pragma once

/**
   @file
   Mass File Loading

   Prototyping future shape of file processing API

   Requirements:
   - maximum I/O performance,
   - managing memory for every file is not a concern for the user,
   - processing a file's content can be done in parallel w/ I/O

   What is this, essentially?  It's a process that sorts files by state
   continuously waiting for certain content to appear, in order to implement a
   pipeline

   Optional:
   - cancelling?
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

/**
 * add @p filepath to the list of files to load.
 *
 * @pre no more than @p maximum_file_count have been pushed already
 */
void push_file(FileLoader &file_loader, char const *filepath, uint32_t tag);

/**
 * add @p filepath to the list of files to load. its size must be @p
 *expected_filesize
 *
 * @pre no more than @p maximum_file_count have been pushed already
 */
void push_file(FileLoader &file_loader,
               char const *filepath,
               size_t expected_filesize,
               uint32_t tag);

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
