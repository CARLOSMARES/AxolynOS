#include <efi.h>
#include <efilib.h>

#include "elf.h"
#include "bootinfo.h"

#define KERNEL_PATH L"\\EFI\\AXOLYNOS\\axolyn.elf"
#define INITRD_PATH L"\\EFI\\AXOLYNOS\\initrd.axfs"
#define BOOTINFO_PADDR 0x1000ULL
#define AX_BOOT_MAGIC 0x41484F4C594E4F53ULL /* "AXOLYNOS" */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    Print(L"[UEFI] AxolynOS UEFI Bootloader\r\n");

    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle,
                              &gEfiLoadedImageProtocolGuid, (void **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"[UEFI] Error: no se pudo obtener LoadedImage\r\n");
        return Status;
    }

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle,
                              &gEfiSimpleFileSystemProtocolGuid, (void **)&Fs);
    if (EFI_ERROR(Status)) {
        Print(L"[UEFI] Error: no se pudo obtener FileSystem\r\n");
        return Status;
    }

    Status = uefi_call_wrapper(Fs->OpenVolume, 2, Fs, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"[UEFI] Error: no se pudo abrir el volumen EFI\r\n");
        return Status;
    }

    /* Cargar kernel.elf */
    EFI_FILE_PROTOCOL *KernelFile = NULL;
    Status = uefi_call_wrapper(Root->Open, 5, Root, &KernelFile, KERNEL_PATH,
                              EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    if (EFI_ERROR(Status)) {
        Print(L"[UEFI] Error: no se encuentra %s\r\n", KERNEL_PATH);
        Print(L"[UEFI] Copia axolyn.elf a ESP:\\EFI\\AXOLYNOS\\\r\n");
        return Status;
    }

    EFI_FILE_INFO *KernelInfo = NULL;
    UINTN InfoSize = SIZE_OF_EFI_FILE_INFO + 1024;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, InfoSize, (void **)&KernelInfo);
    uefi_call_wrapper(KernelFile->GetInfo, 4, KernelFile, &gEfiFileInfoGuid, &InfoSize, KernelInfo);

    UINTN KernelSize = KernelInfo->FileSize;
    VOID *KernelBuf = NULL;
    uefi_call_wrapper(BS->FreePool, 1, KernelInfo);
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, KernelSize, &KernelBuf);
    uefi_call_wrapper(KernelFile->Read, 3, KernelFile, &KernelSize, KernelBuf);
    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);

    Print(L"[UEFI] axolyn.elf cargado (%d bytes)\r\n", KernelSize);

    Elf64_Ehdr *Ehdr = (Elf64_Ehdr *)KernelBuf;
    if (*(UINT32 *)Ehdr->e_ident != ELF_MAGIC) {
        Print(L"[UEFI] Error: ELF inválido\r\n");
        return EFI_LOAD_ERROR;
    }

    Elf64_Phdr *Phdrs = (Elf64_Phdr *)((UINT8 *)KernelBuf + Ehdr->e_phoff);
    for (UINTN i = 0; i < Ehdr->e_phnum; i++) {
        if (Phdrs[i].p_type != PT_LOAD)
            continue;

        UINTN Pages = (Phdrs[i].p_memsz + 0xFFF) / 0x1000;
        EFI_PHYSICAL_ADDRESS DestAddr = (EFI_PHYSICAL_ADDRESS)Phdrs[i].p_paddr;
        Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, Pages, &DestAddr);
        if (EFI_ERROR(Status)) {
            DestAddr = 0;
            Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, Pages, &DestAddr);
            if (EFI_ERROR(Status)) {
                Print(L"[UEFI] Error asignando páginas para PT_LOAD\r\n");
                return Status;
            }
        }

        VOID *Dest = (VOID *)(UINTN)DestAddr;
        SetMem(Dest, Phdrs[i].p_memsz, 0);
        CopyMem(Dest, (UINT8 *)KernelBuf + Phdrs[i].p_offset, Phdrs[i].p_filesz);
        Print(L"[UEFI] PT_LOAD: vaddr=0x%lx paddr=0x%lx filesz=0x%lx memsz=0x%lx\r\n",
              Phdrs[i].p_vaddr, Phdrs[i].p_paddr, Phdrs[i].p_filesz, Phdrs[i].p_memsz);
    }

    /* Cargar initrd.axfs (opcional) */
    EFI_FILE_PROTOCOL *InitrdFile = NULL;
    UINTN InitrdSize = 0;
    VOID *InitrdBuf = NULL;
    Status = uefi_call_wrapper(Root->Open, 5, Root, &InitrdFile, INITRD_PATH,
                              EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    if (!EFI_ERROR(Status)) {
        EFI_FILE_INFO *InitrdInfo = NULL;
        UINTN ISize = SIZE_OF_EFI_FILE_INFO + 1024;
        uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, ISize, (void **)&InitrdInfo);
        uefi_call_wrapper(InitrdFile->GetInfo, 4, InitrdFile, &gEfiFileInfoGuid, &ISize, InitrdInfo);
        InitrdSize = InitrdInfo->FileSize;
        uefi_call_wrapper(BS->FreePool, 1, InitrdInfo);
        uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, InitrdSize, &InitrdBuf);
        uefi_call_wrapper(InitrdFile->Read, 3, InitrdFile, &InitrdSize, InitrdBuf);
        uefi_call_wrapper(InitrdFile->Close, 1, InitrdFile);
        Print(L"[UEFI] initrd.axfs cargado (%d bytes)\r\n", InitrdSize);
    } else {
        Print(L"[UEFI] initrd.axfs no encontrado, continuando...\r\n");
    }

    /* Obtener mapa de memoria UEFI */
    UINTN MapKey = 0;
    UINTN DescSize = 0;
    UINT32 DescVersion = 0;
    UINTN MMapSize = 0;
    uefi_call_wrapper(BS->GetMemoryMap, 5, &MMapSize, NULL, &MapKey, &DescSize, &DescVersion);
    MMapSize += 2 * DescSize + 4096;
    EFI_MEMORY_DESCRIPTOR *MMap = NULL;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, MMapSize, (void **)&MMap);
    uefi_call_wrapper(BS->GetMemoryMap, 5, &MMapSize, MMap, &MapKey, &DescSize, &DescVersion);

    /* Rellenar AxBootInfo para compatibilidad con kernel */
    AxBootInfoUefi *BootInfo = (AxBootInfoUefi *)(UINTN)BOOTINFO_PADDR;
    SetMem(BootInfo, 0x1000, 0);
    BootInfo->magic = AX_BOOT_MAGIC;
    BootInfo->kernel_virt = Ehdr->e_entry;
    BootInfo->initrd_phys = (UINT64)(UINTN)InitrdBuf;
    BootInfo->initrd_size = InitrdSize;
    BootInfo->mmap_ptr = (UINT64)(UINTN)MMap;
    BootInfo->mmap_size = MMapSize;
    BootInfo->mmap_desc_size = DescSize;
    BootInfo->mmap_desc_ver = DescVersion;

    Print(L"[UEFI] Saliendo de BootServices...\r\n");
    uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);

    /* Saltar al kernel */
    typedef void (*KernelEntry)(UINT64 bootinfo);
    KernelEntry Entry = (KernelEntry)(UINTN)Ehdr->e_entry;
    Entry((UINT64)(UINTN)BootInfo);

    for (;;)
        ;

    return EFI_SUCCESS;
}
