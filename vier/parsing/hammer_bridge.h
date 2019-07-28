typedef struct {
  union {
    struct {
      HAllocator base;
      Allocator* ion_allocator;
    };
    char mem[64];
  };
} IHAllocator;

static IHAllocator hallocator_bridge_data(Allocator* allocator);
