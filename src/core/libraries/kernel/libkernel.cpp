// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <thread>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/singleton.h"
#include "core/file_sys/fs.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/kernel/cpu_management.h"
#include "core/libraries/kernel/event_flag/event_flag.h"
#include "core/libraries/kernel/event_queues.h"
#include "core/libraries/kernel/file_system.h"
#include "core/libraries/kernel/libkernel.h"
#include "core/libraries/kernel/memory_management.h"
#include "core/libraries/kernel/thread_management.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/libs.h"
#include "core/linker.h"
#include "core/memory.h"
#ifdef _WIN64
#include <io.h>
#include <objbase.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <core/file_format/psf.h>

namespace Libraries::Kernel {

static u64 g_stack_chk_guard = 0xDEADBEEF54321ABC; // dummy return

static void* PS4_SYSV_ABI sceKernelGetProcParam() {
    auto* linker = Common::Singleton<Core::Linker>::Instance();
    return reinterpret_cast<void*>(linker->GetProcParam());
}

static PS4_SYSV_ABI void stack_chk_fail() {
    UNREACHABLE();
}

int PS4_SYSV_ABI sceKernelMunmap(void* addr, size_t len) {
    LOG_INFO(Kernel_Vmm, "addr = {}, len = {:#x}", fmt::ptr(addr), len);
    auto* memory = Core::Memory::Instance();
    memory->UnmapMemory(std::bit_cast<VAddr>(addr), len);
    return SCE_OK;
}

struct iovec {
    void* iov_base; /* Base	address. */
    size_t iov_len; /* Length. */
};

size_t PS4_SYSV_ABI _writev(int fd, const struct iovec* iov, int iovcn) {
    // weird it gives fd ==0 and writes to stdout , i am not sure if it that is valid (found in
    // openorbis)
    size_t total_written = 0;
    for (int i = 0; i < iovcn; i++) {
        total_written += ::fwrite(iov[i].iov_base, 1, iov[i].iov_len, stdout);
    }
    return total_written;
}

static thread_local int libc_error{};
int* PS4_SYSV_ABI __Error() {
    return &libc_error;
}

int PS4_SYSV_ABI sceKernelMmap(void* addr, u64 len, int prot, int flags, int fd, size_t offset,
                               void** res) {
    LOG_INFO(Kernel_Vmm, "called addr = {}, len = {}, prot = {}, flags = {}, fd = {}, offset = {}",
             fmt::ptr(addr), len, prot, flags, fd, offset);
    auto* h = Common::Singleton<Core::FileSys::HandleTable>::Instance();
    auto* memory = Core::Memory::Instance();
    const auto mem_prot = static_cast<Core::MemoryProt>(prot);
    const auto mem_flags = static_cast<Core::MemoryMapFlags>(flags);
    if (fd == -1) {
        return memory->MapMemory(res, std::bit_cast<VAddr>(addr), len, mem_prot, mem_flags,
                                 Core::VMAType::Flexible);
    } else {
        const uintptr_t handle = h->GetFile(fd)->f.GetFileMapping();
        return memory->MapFile(res, std::bit_cast<VAddr>(addr), len, mem_prot, mem_flags, handle,
                               offset);
    }
}

void* PS4_SYSV_ABI posix_mmap(void* addr, u64 len, int prot, int flags, int fd, u64 offset) {
    void* ptr;
    LOG_INFO(Kernel_Vmm, "posix mmap redirect to sceKernelMmap\n");
    // posix call the difference is that there is a different behaviour when it doesn't return 0 or
    // SCE_OK
    const VAddr ret_addr = (VAddr)__builtin_return_address(0);
    int result = sceKernelMmap(addr, len, prot, flags, fd, offset, &ptr);
    ASSERT(result == 0);
    return ptr;
}

static uint64_t g_mspace_atomic_id_mask = 0;
static uint64_t g_mstate_table[64] = {0};

struct HeapInfoInfo {
    uint64_t size = sizeof(HeapInfoInfo);
    uint32_t flag;
    uint32_t getSegmentInfo;
    uint64_t* mspace_atomic_id_mask;
    uint64_t* mstate_table;
};

void PS4_SYSV_ABI sceLibcHeapGetTraceInfo(HeapInfoInfo* info) {
    info->mspace_atomic_id_mask = &g_mspace_atomic_id_mask;
    info->mstate_table = g_mstate_table;
    info->getSegmentInfo = 0;
}

s64 PS4_SYSV_ABI ps4__write(int d, const void* buf, std::size_t nbytes) {
    if (d <= 2) { // stdin,stdout,stderr
        char* str = strdup((const char*)buf);
        if (str[nbytes - 1] == '\n')
            str[nbytes - 1] = 0;
        LOG_INFO(Tty, "{}", str);
        free(str);
        return nbytes;
    }
    LOG_ERROR(Kernel, "(STUBBED) called d = {} nbytes = {} ", d, nbytes);
    UNREACHABLE(); // normal write , is it a posix call??
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceKernelConvertUtcToLocaltime(time_t time, time_t* local_time,
                                                struct OrbisTimesec* st, unsigned long* dst_sec) {
    LOG_TRACE(Kernel, "Called");
    const auto* time_zone = std::chrono::current_zone();
    auto info = time_zone->get_info(std::chrono::system_clock::now());

    *local_time = info.offset.count() + info.save.count() * 60 + time;

    if (st != nullptr) {
        st->t = time;
        st->west_sec = info.offset.count() * 60;
        st->dst_sec = info.save.count() * 60;
    }

    if (dst_sec != nullptr) {
        *dst_sec = info.save.count() * 60;
    }

    return ORBIS_OK;
}

int PS4_SYSV_ABI sceKernelGetCompiledSdkVersion(int* ver) {
    auto* param_sfo = Common::Singleton<PSF>::Instance();
    int version = param_sfo->GetInteger("SYSTEM_VER");
    LOG_INFO(Kernel, "returned system version = {:#x}", version);
    *ver = version;
    return ORBIS_OK;
}

s64 PS4_SYSV_ABI ps4__read(int d, void* buf, u64 nbytes) {
    ASSERT_MSG(d == 0, "d is not 0!");

    return static_cast<s64>(
        strlen(std::fgets(static_cast<char*>(buf), static_cast<int>(nbytes), stdin)));
}

s32 PS4_SYSV_ABI sceKernelLoadStartModule(const char* moduleFileName, size_t args, const void* argp,
                                          u32 flags, const void* pOpt, int* pRes) {
    LOG_INFO(Lib_Kernel, "called filename = {}, args = {}", moduleFileName, args);

    if (flags != 0) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
    const auto path = mnt->GetHostFile(moduleFileName);

    // Load PRX module and relocate any modules that import it.
    auto* linker = Common::Singleton<Core::Linker>::Instance();
    u32 handle = linker->LoadModule(path, true);
    if (handle == -1) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    auto* module = linker->GetModule(handle);
    linker->RelocateAnyImports(module);

    // If the new module has a TLS image, trigger its load when TlsGetAddr is called.
    if (module->tls.image_size != 0) {
        linker->AdvanceGenerationCounter();
    }

    // Retrieve and verify proc param according to libkernel.
    u64* param = module->GetProcParam<u64*>();
    ASSERT_MSG(!param || param[0] >= 0x18, "Invalid module param size: {}", param[0]);
    module->Start(args, argp, param);

    return handle;
}

s32 PS4_SYSV_ABI sceKernelDlsym(s32 handle, const char* symbol, void** addrp) {
    auto* linker = Common::Singleton<Core::Linker>::Instance();
    auto* module = linker->GetModule(handle);
    *addrp = module->FindByName(symbol);
    if (*addrp == nullptr) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    return ORBIS_OK;
}

static constexpr size_t ORBIS_DBG_MAX_NAME_LENGTH = 256;

struct OrbisModuleInfoForUnwind {
    u64 st_size;
    std::array<char, ORBIS_DBG_MAX_NAME_LENGTH> name;
    VAddr eh_frame_hdr_addr;
    VAddr eh_frame_addr;
    u64 eh_frame_size;
    VAddr seg0_addr;
    u64 seg0_size;
};

s32 PS4_SYSV_ABI sceKernelGetModuleInfoForUnwind(VAddr addr, int flags,
                                                 OrbisModuleInfoForUnwind* info) {
    if (flags >= 3) {
        std::memset(info, 0, sizeof(OrbisModuleInfoForUnwind));
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if (!info) {
        return ORBIS_KERNEL_ERROR_EFAULT;
    }
    if (info->st_size <= sizeof(OrbisModuleInfoForUnwind)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    // Find module that contains specified address.
    LOG_INFO(Lib_Kernel, "called addr = {:#x}, flags = {:#x}", addr, flags);
    auto* linker = Common::Singleton<Core::Linker>::Instance();
    auto* module = linker->FindByAddress(addr);
    const auto mod_info = module->GetModuleInfoEx();

    // Fill in module info.
    info->name = mod_info.name;
    info->eh_frame_hdr_addr = mod_info.eh_frame_hdr_addr;
    info->eh_frame_addr = mod_info.eh_frame_addr;
    info->eh_frame_size = mod_info.eh_frame_size;
    info->seg0_addr = mod_info.segments[0].address;
    info->seg0_size = mod_info.segments[0].size;
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceKernelGetModuleInfoFromAddr(VAddr addr, int flags,
                                                Core::OrbisKernelModuleInfoEx* info) {
    LOG_INFO(Lib_Kernel, "called addr = {:#x}, flags = {:#x}", addr, flags);
    auto* linker = Common::Singleton<Core::Linker>::Instance();
    auto* module = linker->FindByAddress(addr);
    *info = module->GetModuleInfoEx();
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceKernelDebugRaiseException() {
    UNREACHABLE();
    return 0;
}

int PS4_SYSV_ABI sceKernelGetCpumode() {
    return 0;
}

void PS4_SYSV_ABI sched_yield() {
    return std::this_thread::yield();
}

int PS4_SYSV_ABI sceKernelUuidCreate(OrbisKernelUuid* orbisUuid) {
#ifdef _WIN64
    UUID uuid;
    UuidCreate(&uuid);
    orbisUuid->timeLow = uuid.Data1;
    orbisUuid->timeMid = uuid.Data2;
    orbisUuid->timeHiAndVersion = uuid.Data3;
    orbisUuid->clockSeqHiAndReserved = uuid.Data4[0];
    orbisUuid->clockSeqLow = uuid.Data4[1];
    for (int i = 0; i < 6; i++) {
        orbisUuid->node[i] = uuid.Data4[2 + i];
    }
#else
    LOG_ERROR(Kernel, "sceKernelUuidCreate: Add linux");
#endif
    return 0;
}

void LibKernel_Register(Core::Loader::SymbolsResolver* sym) {
    // obj
    LIB_OBJ("f7uOxY9mM1U", "libkernel", 1, "libkernel", 1, 1, &g_stack_chk_guard);
    // memory
    LIB_FUNCTION("OMDRKKAZ8I4", "libkernel", 1, "libkernel", 1, 1, sceKernelDebugRaiseException);
    LIB_FUNCTION("rTXw65xmLIA", "libkernel", 1, "libkernel", 1, 1, sceKernelAllocateDirectMemory);
    LIB_FUNCTION("B+vc2AO2Zrc", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelAllocateMainDirectMemory);
    LIB_FUNCTION("C0f7TJcbfac", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelAvailableDirectMemorySize);
    LIB_FUNCTION("hwVSPCmp5tM", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelCheckedReleaseDirectMemory);
    LIB_FUNCTION("rVjRvHJ0X6c", "libkernel", 1, "libkernel", 1, 1, sceKernelVirtualQuery);
    LIB_FUNCTION("pO96TwzOm5E", "libkernel", 1, "libkernel", 1, 1, sceKernelGetDirectMemorySize);
    LIB_FUNCTION("NcaWUxfMNIQ", "libkernel", 1, "libkernel", 1, 1, sceKernelMapNamedDirectMemory);
    LIB_FUNCTION("L-Q3LEjIbgA", "libkernel", 1, "libkernel", 1, 1, sceKernelMapDirectMemory);
    LIB_FUNCTION("WFcfL2lzido", "libkernel", 1, "libkernel", 1, 1, sceKernelQueryMemoryProtection);
    LIB_FUNCTION("BHouLQzh0X0", "libkernel", 1, "libkernel", 1, 1, sceKernelDirectMemoryQuery);
    LIB_FUNCTION("MBuItvba6z8", "libkernel", 1, "libkernel", 1, 1, sceKernelReleaseDirectMemory);
    LIB_FUNCTION("cQke9UuBQOk", "libkernel", 1, "libkernel", 1, 1, sceKernelMunmap);
    LIB_FUNCTION("mL8NDH86iQI", "libkernel", 1, "libkernel", 1, 1, sceKernelMapNamedFlexibleMemory);
    LIB_FUNCTION("aNz11fnnzi4", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelAvailableFlexibleMemorySize);
    LIB_FUNCTION("IWIBBdTHit4", "libkernel", 1, "libkernel", 1, 1, sceKernelMapFlexibleMemory);
    LIB_FUNCTION("p5EcQeEeJAE", "libkernel", 1, "libkernel", 1, 1,
                 _sceKernelRtldSetApplicationHeapAPI);
    LIB_FUNCTION("wzvqT4UqKX8", "libkernel", 1, "libkernel", 1, 1, sceKernelLoadStartModule);
    LIB_FUNCTION("LwG8g3niqwA", "libkernel", 1, "libkernel", 1, 1, sceKernelDlsym);
    LIB_FUNCTION("RpQJJVKTiFM", "libkernel", 1, "libkernel", 1, 1, sceKernelGetModuleInfoForUnwind);
    LIB_FUNCTION("f7KBOafysXo", "libkernel", 1, "libkernel", 1, 1, sceKernelGetModuleInfoFromAddr);
    LIB_FUNCTION("VOx8NGmHXTs", "libkernel", 1, "libkernel", 1, 1, sceKernelGetCpumode);
    LIB_FUNCTION("Xjoosiw+XPI", "libkernel", 1, "libkernel", 1, 1, sceKernelUuidCreate);

    // equeue
    LIB_FUNCTION("D0OdFMjp46I", "libkernel", 1, "libkernel", 1, 1, sceKernelCreateEqueue);
    LIB_FUNCTION("jpFjmgAC5AE", "libkernel", 1, "libkernel", 1, 1, sceKernelDeleteEqueue);
    LIB_FUNCTION("fzyMKs9kim0", "libkernel", 1, "libkernel", 1, 1, sceKernelWaitEqueue);
    LIB_FUNCTION("vz+pg2zdopI", "libkernel", 1, "libkernel", 1, 1, sceKernelGetEventUserData);
    LIB_FUNCTION("4R6-OvI2cEA", "libkernel", 1, "libkernel", 1, 1, sceKernelAddUserEvent);
    LIB_FUNCTION("WDszmSbWuDk", "libkernel", 1, "libkernel", 1, 1, sceKernelAddUserEventEdge);
    LIB_FUNCTION("F6e0kwo4cnk", "libkernel", 1, "libkernel", 1, 1, sceKernelTriggerUserEvent);
    LIB_FUNCTION("LJDwdSNTnDg", "libkernel", 1, "libkernel", 1, 1, sceKernelDeleteUserEvent);

    // misc
    LIB_FUNCTION("WslcK1FQcGI", "libkernel", 1, "libkernel", 1, 1, sceKernelIsNeoMode);
    LIB_FUNCTION("Ou3iL1abvng", "libkernel", 1, "libkernel", 1, 1, stack_chk_fail);
    LIB_FUNCTION("9BcDykPmo1I", "libkernel", 1, "libkernel", 1, 1, __Error);
    LIB_FUNCTION("BPE9s9vQQXo", "libkernel", 1, "libkernel", 1, 1, posix_mmap);
    LIB_FUNCTION("BPE9s9vQQXo", "libScePosix", 1, "libkernel", 1, 1, posix_mmap);
    LIB_FUNCTION("YSHRBRLn2pI", "libkernel", 1, "libkernel", 1, 1, _writev);
    LIB_FUNCTION("959qrazPIrg", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcParam);
    LIB_FUNCTION("-o5uEDpN+oY", "libkernel", 1, "libkernel", 1, 1, sceKernelConvertUtcToLocaltime);
    LIB_FUNCTION("WB66evu8bsU", "libkernel", 1, "libkernel", 1, 1, sceKernelGetCompiledSdkVersion);
    LIB_FUNCTION("DRuBt2pvICk", "libkernel", 1, "libkernel", 1, 1, ps4__read);

    Libraries::Kernel::fileSystemSymbolsRegister(sym);
    Libraries::Kernel::timeSymbolsRegister(sym);
    Libraries::Kernel::pthreadSymbolsRegister(sym);
    Libraries::Kernel::RegisterKernelEventFlag(sym);

    // temp
    LIB_FUNCTION("NWtTN10cJzE", "libSceLibcInternalExt", 1, "libSceLibcInternal", 1, 1,
                 sceLibcHeapGetTraceInfo);
    LIB_FUNCTION("FxVZqBAA7ks", "libkernel", 1, "libkernel", 1, 1, ps4__write);
    LIB_FUNCTION("6XG4B33N09g", "libScePosix", 1, "libkernel", 1, 1, sched_yield);
}

} // namespace Libraries::Kernel
