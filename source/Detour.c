/*
 * GoldHEN Plugin SDK - a prx hook/patch sdk for Orbis OS
 *
 * Credits
 * - OSM <https://github.com/OSM-Made>
 * - jocover <https://github.com/jocover>
 * - bucanero <https://github.com/bucanero>
 * - OpenOrbis Team <https://github.com/OpenOrbis>
 * - SiSTRo <https://github.com/SiSTR0>
 */

#include "Common.h"
#include "HDE64.h"

size_t Detour_GetInstructionSize(Detour *This, uint64_t Address, size_t MinSize);
void Detour_WriteJump64(Detour *This, void *Address, uint64_t Destination);
void Detour_WriteJump32(Detour *This, void *Address, uint64_t Destination);
uint64_t Detour_GetJumpAddress64(Detour *This, void *Address);
void *Detour_DetourFunction(Detour *This, uint64_t FunctionPtr, void *HookPtr);
void *Detour_DetourFunction64(Detour *This, uint64_t FunctionPtr, void *HookPtr);
void *Detour_DetourFunction32(Detour *This, uint64_t FunctionPtr, void *HookPtr);
void Detour_RestoreFunction(Detour *This);
void Detour_Construct(Detour *This, DetourMode Mode);
void Detour_Destroy(Detour *This);

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define R_ADDRESS_SIZE 4
#define A_ADDRESS_SIZE 8

InstructionPatch jmpPatch = {
        .name = "jmp",
        .originInstructionSize = 5,
        .patchInstruction = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
        .patchInstructionSize = 14,
        .originInstructionPattern = {0xE9},
        .originInstructionPatternSize = 1,
};

InstructionPatch movRaxPatch = {
        .name = "mov_rax",
        .originInstructionSize = 7,
        .patchInstruction = {0x48, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .patchInstructionSize = 10,
        .originInstructionPattern = {0x48, 0x8B, 0x05},
        .originInstructionPatternSize = 3,
};

InstructionPatch leaRaxPatch = {
        .name = "lea_rax",
        .originInstructionSize = 7,
        .patchInstruction = {0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .patchInstructionSize = 10,
        .originInstructionPattern = {0x48, 0x8D, 0x05},
        .originInstructionPatternSize = 3,
};

InstructionPatch leaRbxPatch = {
        .name = "lea_rbx",
        .originInstructionSize = 7,
        .patchInstruction = {0x48, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .patchInstructionSize = 10,
        .originInstructionPattern = {0x48, 0x8D, 0x1D},
        .originInstructionPatternSize = 3,
};

InstructionPatch leaRcxPatch = {
        .name = "lea_rcx",
        .originInstructionSize = 7,
        .patchInstruction = {0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .patchInstructionSize = 10,
        .originInstructionPattern = {0x48, 0x8D, 0x0D},
        .originInstructionPatternSize = 3,
};

static const InstructionPatch *const PatchList[] = {
        &jmpPatch,
        &movRaxPatch,
        &leaRaxPatch,
        &leaRbxPatch,
        &leaRcxPatch,
};

size_t Detour_GetInstructionSize(Detour *This, uint64_t Address, size_t MinSize) {
    size_t InstructionSize = 0;
    uint32_t temp;
    if (!Address) return 0;

    This->LastInstructionPatch = NULL;
    while (InstructionSize < MinSize) {
        hde64s hs;
        temp = hde64_disasm((void *) (Address + InstructionSize), &hs);

        if (hs.flags & F_ERROR) return 0;

        InstructionSize += temp;
    }

    uint32_t LastInstructionSize = temp;
    void *LastInstruction = (void *) (Address + InstructionSize - LastInstructionSize);
    for (int i = 0; i < ARRAY_SIZE(PatchList); i++) {
        if (LastInstructionSize == PatchList[i]->originInstructionSize &&
            memcmp(LastInstruction, PatchList[i]->originInstructionPattern,
                   PatchList[i]->originInstructionPatternSize) == 0) {
            This->LastInstructionPatch = PatchList[i];
            break;
        }
    }

    return InstructionSize;
}

void Detour_WriteJump64(Detour *This, void *Address, uint64_t Destination) {
    // Write the address of our hook to the instruction.
    *(uint64_t * )(This->JumpInstructions64 + 6) = Destination;

    sceKernelMprotect((void *) Address, sizeof(This->JumpInstructions64), VM_PROT_ALL);
    memcpy(Address, This->JumpInstructions64, sizeof(This->JumpInstructions64));
}

void Detour_WriteJump32(Detour *This, void *Address, uint64_t Destination) {
    uint32_t Offset = (uint32_t)(Destination - ((uint64_t) Address + sizeof(This->JumpInstructions32)));

    // Write the address of our hook to the instruction.
    *(uint32_t * )(This->JumpInstructions32 + 1) = Offset;

    sceKernelMprotect((void *) Address, sizeof(This->JumpInstructions32), VM_PROT_ALL);
    memcpy(Address, This->JumpInstructions32, sizeof(This->JumpInstructions32));
}

uint64_t Detour_GetJumpAddress64(Detour *This, void *Address) {
    return *(uint64_t * )((uint64_t)Address + 6);
}

void *Detour_DetourFunction(Detour *This, uint64_t FunctionPtr, void *HookPtr) {
    switch (This->Mode) {
        case DetourMode_x32:
            return Detour_DetourFunction32(This, FunctionPtr, HookPtr);
        case DetourMode_x64:
            return Detour_DetourFunction64(This, FunctionPtr, HookPtr);
    }
    return NULL;
}

void *Detour_DetourFunction64(Detour *This, uint64_t FunctionPtr, void *HookPtr) {
    if (!FunctionPtr  || !HookPtr) {
#if (DEBUG) == 1
        klog("[Detour] %s: FunctionPtr or HookPtr NULL (%p -> %p)\n", __FUNCTION__, (void*)FunctionPtr, HookPtr);
#endif
        return NULL;
    }

    uint32_t InstructionSize = Detour_GetInstructionSize(This, FunctionPtr, sizeof(This->JumpInstructions64));

#if (DEBUG) == 1
    klog("[Detour] %s: - InstructionSize: %u\n", __FUNCTION__, InstructionSize);
#endif

    if (InstructionSize < sizeof(This->JumpInstructions64)) {
#if (DEBUG) == 1
        klog("[Detour] %s: Hooking Requires a minimum of %d bytes to write jump!\n", __FUNCTION__, (int)sizeof(This->JumpInstructions64));
#endif
        return NULL;
    }

    int res = sceKernelMmap(0, sizeof(This->JumpInstructions64), VM_PROT_ALL, 0x1000 | 0x2, -1, 0, &This->TrampolinePtr);

    if (res < 0 || This->TrampolinePtr == 0) {
#if (DEBUG) == 1
        klog("[Detour] %s: sceKernelMmap failed (0x%X).\n", __FUNCTION__, res);
#endif
        return 0;
    }

    Detour_WriteJump64(This, This->TrampolinePtr, (uint64_t) HookPtr);

    // Save Pointers for later
    This->FunctionPtr = (void *) FunctionPtr;
    This->HookPtr = HookPtr;

    // Set protection.
    sceKernelMprotect((void *) FunctionPtr, InstructionSize, VM_PROT_ALL);

    //Allocate Executable memory for stub and write instructions to stub and a jump back to original execution.
    This->StubSize = (InstructionSize + sizeof(This->JumpInstructions64));
    res = sceKernelMmap(0, This->StubSize, VM_PROT_ALL, 0x1000 | 0x2, -1, 0, &This->StubPtr);

    if (res < 0 || This->StubPtr == 0) {
#if (DEBUG) == 1
        klog("[Detour] %s: sceKernelMmap failed (0x%X).\n", __FUNCTION__, res);
#endif
        return 0;
    }

    memcpy(This->StubPtr, (void *) FunctionPtr, InstructionSize);
    Detour_WriteJump64(This, (void *) ((uint64_t) This->StubPtr + InstructionSize), (uint64_t)(FunctionPtr + InstructionSize));

    // Write jump from function to hook.
    memset((void *) FunctionPtr, 0x90, InstructionSize);
    Detour_WriteJump64(This, (void *) FunctionPtr, (uint64_t) This->TrampolinePtr);

#if (DEBUG) == 1
    klog("[Detour] %s: Detour Written Successfully! (FunctionPtr: %p - HookPtr: %p - HookPtrTrampoline: %p - StubPtr: %p - StubSize: %zu)\n", __FUNCTION__, This->FunctionPtr, This->HookPtr, This->TrampolinePtr, This->StubPtr, This->StubSize);
#endif

    return This->StubPtr;
}

void *Detour_DetourFunction32(Detour *This, uint64_t FunctionPtr, void *HookPtr) {
    if (!FunctionPtr || !HookPtr) {
#if (DEBUG) == 1
        klog("[Detour] %s: FunctionPtr or HookPtr NULL (%p -> %p)\n", __FUNCTION__, (void*)FunctionPtr, HookPtr);
#endif
        return NULL;
    }

    size_t InstructionSize = Detour_GetInstructionSize(This, FunctionPtr, sizeof(This->JumpInstructions32));

#if (DEBUG) == 1
    klog("[Detour] %s: - InstructionSize: %zu\n", __FUNCTION__, InstructionSize);
#endif

    if (InstructionSize < sizeof(This->JumpInstructions32)) {
#if (DEBUG) == 1
        klog("[Detour] %s: Hooking Requires a minimum of %d bytes to write jump!\n", __FUNCTION__, (int)sizeof(This->JumpInstructions64));
#endif
        return NULL;
    }

    if ((memcmp((void*) FunctionPtr, This->JumpInstructions32, 1) == 0) && InstructionSize == sizeof(This->JumpInstructions32)) {
        uint32_t TrampolineOffest = *(uint32_t*)(FunctionPtr + 1);
#if (DEBUG) == 1
        klog("[Detour] %s: Trampoline found at %p (Offset: %X). Detour called for Trampoline...\n", __FUNCTION__, (void*)(TrampolineOffest + FunctionPtr + 5), TrampolineOffest);
#endif
        This->Mode = DetourMode_x64;
        return Detour_DetourFunction64(This, (uint64_t)(TrampolineOffest + FunctionPtr + 5), HookPtr);
    }

    This->TrampolinePtr = malloc(sizeof(This->JumpInstructions64));

    if (This->TrampolinePtr == 0) {
#if (DEBUG) == 1
        klog("[Detour] %s: malloc failed.\n", __FUNCTION__);
#endif
        return 0;
    }

    Detour_WriteJump64(This, This->TrampolinePtr, (uint64_t) HookPtr);

    // Save Pointers for later
    This->FunctionPtr = (void *)FunctionPtr;
    This->HookPtr = HookPtr;

    // Set protection.
    sceKernelMprotect((void *) FunctionPtr, InstructionSize, VM_PROT_ALL);

    //Allocate Executable memory for stub and write instructions to stub and a jump back to original execution.
    This->StubSize = (InstructionSize + sizeof(This->JumpInstructions64));

    if (This->LastInstructionPatch != NULL) {
        This->StubSize += This->LastInstructionPatch->patchInstructionSize;
    }

    int res = sceKernelMmap(0, This->StubSize, VM_PROT_ALL, 0x1000 | 0x2, -1, 0, &This->StubPtr);

    if (res < 0 || This->StubPtr == 0) {
#if (DEBUG) == 1
        klog("[Detour] %s: sceKernelMmap failed (0x%X).\n", __FUNCTION__, res);
#endif
        return 0;
    }

    if (This->LastInstructionPatch) {
        // Stub: [Origin function code without last line] [last line patch] [Jump to (origin function + InstructionSize)] [Origin function code last line]
#if (DEBUG) == 1
        klog("[Detour] %s: Using %s instruction patch\n", __FUNCTION__, This->LastInstructionPatch->name);
#endif
        size_t InstructionSizeWithoutLastLine = InstructionSize - This->LastInstructionPatch->originInstructionSize;
        void *lastInstructionOrignPtr = (void *) FunctionPtr + InstructionSizeWithoutLastLine;
        void *lastInstructionStubPtr = This->StubPtr + InstructionSizeWithoutLastLine;
        // Copy origin function code without last line to stub
        memcpy(This->StubPtr, (void *) FunctionPtr, InstructionSizeWithoutLastLine);
        // Get relative address from original instruction
        int32_t offset = 0;
        memcpy(&offset, (void *) FunctionPtr + InstructionSize - R_ADDRESS_SIZE, R_ADDRESS_SIZE);
        // Calculate absolute address
        void *AbsoluteAddress = (void *) FunctionPtr + InstructionSize + offset;
        // Write patch instruction
        memcpy(lastInstructionStubPtr,
               This->LastInstructionPatch->patchInstruction,
               This->LastInstructionPatch->patchInstructionSize - A_ADDRESS_SIZE);
        memcpy(lastInstructionStubPtr + This->LastInstructionPatch->patchInstructionSize - A_ADDRESS_SIZE,
               &AbsoluteAddress,
               A_ADDRESS_SIZE);
        // Write jump to original function
        Detour_WriteJump64(This, (void *) ((uint64_t) This->StubPtr + InstructionSizeWithoutLastLine +
                                           This->LastInstructionPatch->patchInstructionSize),
                           (uint64_t) (FunctionPtr + InstructionSize));
        // Backup last instruction
        memcpy(This->StubPtr + This->StubSize - This->LastInstructionPatch->originInstructionSize,
               lastInstructionOrignPtr,
               This->LastInstructionPatch->originInstructionSize);
    } else {
        // Stub: [Origin function code] [Jump to (origin function + InstructionSize)]
        memcpy(This->StubPtr, (void *) FunctionPtr, InstructionSize);
        Detour_WriteJump64(This, (void *) ((uint64_t) This->StubPtr + InstructionSize),
                           (uint64_t) (FunctionPtr + InstructionSize));
    }

    // Write jump from function to hook.
    memset((void *) FunctionPtr, 0x90, InstructionSize);
    Detour_WriteJump32(This, (void *) FunctionPtr, (uint64_t) This->TrampolinePtr);

#if (DEBUG) == 1
    klog("[Detour] %s: Detour Written Successfully! (FunctionPtr: %p - HookPtr: %p - HookPtrTrampoline: %p - StubPtr: %p - StubSize: %zu)\n", __FUNCTION__, This->FunctionPtr, This->HookPtr, This->TrampolinePtr, This->StubPtr, This->StubSize);
#endif

    return This->StubPtr;
}

void Detour_RestoreFunction(Detour *This) {
    void* RestorePtr = 0;
    size_t RestoreSize = 0;
    uint64_t JmpAddress = 0;

    if (This->StubPtr) {
        switch (This->Mode) {
            case DetourMode_x32:
                RestorePtr = This->FunctionPtr;
                RestoreSize = This->StubSize - sizeof(This->JumpInstructions64);
                if (This->LastInstructionPatch) {
                    // Restore original last instruction
                    RestoreSize -= This->LastInstructionPatch->patchInstructionSize;
                    size_t InstructionSize = RestoreSize;
                    memcpy(This->StubPtr + InstructionSize - This->LastInstructionPatch->originInstructionSize,
                           This->StubPtr + This->StubSize - This->LastInstructionPatch->originInstructionSize,
                           This->LastInstructionPatch->originInstructionSize);
                }
                break;
            
            case DetourMode_x64:
                JmpAddress = Detour_GetJumpAddress64(This, This->FunctionPtr);

                RestorePtr = (JmpAddress != (uint64_t)This->TrampolinePtr) ? This->TrampolinePtr : This->FunctionPtr;
                RestoreSize = This->StubSize - sizeof(This->JumpInstructions64);
                break;
        }

        if (RestorePtr != 0 && RestoreSize != 0) {
            sceKernelMprotect((void *) RestorePtr, RestoreSize, VM_PROT_ALL);
            memcpy((void *) RestorePtr, This->StubPtr, RestoreSize);

#if (DEBUG) == 1
            klog("[Detour] %s: (%p) has been Restored Successfully!\n", __FUNCTION__, RestorePtr);
#endif
        }
    }
}

void Detour_Construct(Detour *This, DetourMode Mode) {
    This->StubPtr = 0;
    This->StubSize = 0;
    This->FunctionPtr = 0;
    This->TrampolinePtr = 0;
    This->HookPtr = 0;
    uint8_t ji32[] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    memcpy(This->JumpInstructions32, ji32, sizeof(ji32));
    uint8_t ji64[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    memcpy(This->JumpInstructions64, ji64, sizeof(ji64));
    This->Mode = Mode;
}

void Detour_Destroy(Detour *This) {
    Detour_RestoreFunction(This);

    // Clean up
    sceKernelMunmap(This->StubPtr, This->StubSize);
}
