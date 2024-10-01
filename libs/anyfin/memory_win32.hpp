
#define FIN_MEMORY_HPP_IMPL

#include "anyfin/win32.hpp"

#include "anyfin/memory.hpp"

namespace Fin {

static Memory_Region reserve_virtual_memory (usize size) {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  
  const auto aligned_size = align_forward(size, system_info.dwPageSize);

  auto memory = VirtualAlloc(0, aligned_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  return Memory_Region { (u8 *) memory, aligned_size };
}

static void free_virtual_memory (Memory_Region &memory) {
  VirtualFree(memory.memory, memory.size, MEM_RELEASE);
}

}
