#include <assert.h>

void* iha_alloc(HAllocator* self_, size_t size) {
  IHAllocator* self = (IHAllocator*) self_;
  return generic_alloc(self->ion_allocator, size, alignof(void*));
}

void* iha_realloc(HAllocator* self_, void* ptr, size_t size) {
  IHAllocator* self = (IHAllocator*) self_;
  assert(0);
}

void iha_free(HAllocator* self_, void* ptr) {
  IHAllocator* self = (IHAllocator*) self_;
  generic_free(self->ion_allocator, ptr);
}

IHAllocator hallocator_bridge_data(Allocator* a) {
  return (IHAllocator) {
    .base.alloc = iha_alloc,
    .base.realloc = iha_realloc,
    .base.free = iha_free,
    .ion_allocator = a? a:current_allocator,
  };
}
