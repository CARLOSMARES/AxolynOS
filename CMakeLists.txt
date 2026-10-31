cmake_minimum_required(VERSION 3.20)
project(AxolynOS LANGUAGES C CXX ASM)

# ---------------------------------------------------------------------------
# AxolynOS - build con CMake
#
#   cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release
#   cmake --build cmake-build -j
#
# Genera en cmake-build/build/:
#   axolyn.elf, initrd.axfs, axolynos.img, AxolynOS.iso,
#   apps/, modules/, lib/, tools/, efi/
#
# No se usa ningun script: CMake invoca gcc/g++/as/ld/nasm/xorriso.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(BUILD ${CMAKE_BINARY_DIR}/build)
set(TOOLS ${BUILD}/tools)
set(STAGE ${BUILD}/disk/root)

find_program(NASM nasm REQUIRED)
find_program(XORRISO xorriso REQUIRED)
find_program(MKFS_VFAT mkfs.vfat REQUIRED)
find_program(MMD mmd REQUIRED)
find_program(MCOPY mcopy REQUIRED)
find_program(DD dd REQUIRED)

# ---------------------------------------------------------------------------
# Banderas
# ---------------------------------------------------------------------------
set(KERNEL_CXX_FLAGS
    -O2 -g -ffreestanding -nostdlib -fno-exceptions -fno-rtti
    -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie
    -fno-stack-protector -fno-asynchronous-unwind-tables
    -Wall -Wextra -Wno-unused-parameter)

set(USER_CXX_FLAGS
    -O2 -g -ffreestanding -nostdlib -fno-exceptions -fno-rtti
    -mno-red-zone -fPIE -fno-stack-protector
    -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter)

set(HOST_CXX_FLAGS -O2 -Wall -Wextra)

set(KERNEL_INCLUDES
    ${ROOT}/Kernel/include ${ROOT}/Kernel/vm ${ROOT}/apis/include)

set(USER_INCLUDES ${ROOT}/apis/include)

# ---------------------------------------------------------------------------
# 1. Bootloader BIOS (NASM)
# ---------------------------------------------------------------------------
set(STAGE1 ${BUILD}/stage1.bin)
set(STAGE2 ${BUILD}/stage2.bin)

add_custom_command(
    OUTPUT ${STAGE1}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD}
    COMMAND ${NASM} -f bin ${ROOT}/Kernel/bootloader/stage1.asm -o ${STAGE1}
    DEPENDS ${ROOT}/Kernel/bootloader/stage1.asm
    COMMENT "NASM stage1.bin")
add_custom_command(
    OUTPUT ${STAGE2}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD}
    COMMAND ${NASM} -f bin ${ROOT}/Kernel/bootloader/stage2.asm -o ${STAGE2}
    DEPENDS ${ROOT}/Kernel/bootloader/stage2.asm
    COMMENT "NASM stage2.bin")
add_custom_target(bootloader DEPENDS ${STAGE1} ${STAGE2})

# ---------------------------------------------------------------------------
# 2. Herramientas de host
# ---------------------------------------------------------------------------
file(GLOB TOOL_SRCS CONFIGURE_DEPENDS ${ROOT}/tools/*.cpp)
set(TOOL_BINS)
foreach(src ${TOOL_SRCS})
    get_filename_component(name ${src} NAME_WE)
    set(bin ${TOOLS}/${name})
    add_executable(${name}_tool ${src})
    target_compile_options(${name}_tool PRIVATE ${HOST_CXX_FLAGS})
    target_include_directories(${name}_tool PRIVATE
        ${ROOT}/Kernel/include ${ROOT}/apis/include)
    set_target_properties(${name}_tool PROPERTIES
        OUTPUT_NAME ${name}
        RUNTIME_OUTPUT_DIRECTORY ${TOOLS})
    list(APPEND TOOL_BINS ${bin})
endforeach()
add_custom_target(tools DEPENDS ${TOOL_BINS})
set(MKDISK ${TOOLS}/mkdisk)
set(MKINITRAMFS ${TOOLS}/mkinitramfs)
set(MKMOD ${TOOLS}/mkmod)
set(MKAXE ${TOOLS}/mkaxe)
set(MKAXL ${TOOLS}/mkaxl)

# ---------------------------------------------------------------------------
# 3. Compilador Axolyn (axc) y runner (axrun)
# ---------------------------------------------------------------------------
file(GLOB AXC_SRCS CONFIGURE_DEPENDS ${ROOT}/compiladores/axc/src/*.cpp)
list(REMOVE_ITEM AXC_SRCS ${ROOT}/compiladores/axc/src/axrun.cpp)

add_executable(axc ${AXC_SRCS})
target_compile_options(axc PRIVATE ${HOST_CXX_FLAGS})
target_include_directories(axc PRIVATE
    ${ROOT}/compiladores/axc/include ${ROOT}/Kernel/vm)
set_target_properties(axc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD})

add_executable(axrun
    ${ROOT}/Kernel/vm/axvm.cpp
    ${ROOT}/compiladores/axc/src/axrun.cpp)
target_compile_options(axrun PRIVATE ${HOST_CXX_FLAGS})
target_include_directories(axrun PRIVATE
    ${ROOT}/Kernel/vm ${ROOT}/compiladores/axc/include)
set_target_properties(axrun PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD})

# ---------------------------------------------------------------------------
# 4. Kernel
# ---------------------------------------------------------------------------
file(GLOB KERNEL_CPP CONFIGURE_DEPENDS
    ${ROOT}/Kernel/src/*.cpp ${ROOT}/Kernel/vm/*.cpp)
file(GLOB KERNEL_ASM CONFIGURE_DEPENDS ${ROOT}/Kernel/src/*.S)

add_executable(axolyn_kernel ${KERNEL_CPP} ${KERNEL_ASM})
target_include_directories(axolyn_kernel PRIVATE ${KERNEL_INCLUDES})
target_compile_options(axolyn_kernel PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:-mno-red-zone;-fno-pic;-fno-pie>)
target_link_options(axolyn_kernel PRIVATE
    -nostdlib -no-pie -Wl,--build-id=none
    -Wl,-T,${ROOT}/Kernel/linker/kernel.ld
    -Wl,-z,max-page-size=0x1000 -Wl,--no-dynamic-linker
    -Wl,-Map,${BUILD}/axolyn.elf.map)
set_target_properties(axolyn_kernel PROPERTIES
    OUTPUT_NAME axolyn
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY ${BUILD}
    LINK_DEPENDS ${ROOT}/Kernel/linker/kernel.ld
    CXX_STANDARD 17 CXX_EXTENSIONS ON)

set(AXOLYN_ELF ${BUILD}/axolyn.elf)
add_custom_target(axolyn_kernel_img DEPENDS ${AXOLYN_ELF})

# ---------------------------------------------------------------------------
# 5. Modulos del kernel (.axmod)
# ---------------------------------------------------------------------------
file(GLOB MODULE_DIRS CONFIGURE_DEPENDS LIST_DIRECTORIES true ${ROOT}/Kernel/modules/*)
set(MODULE_BASE_0 0xFFFFFFFF82200000)
set(MODULE_BASE_1 0xFFFFFFFF82300000)
set(MODULE_BASE_2 0xFFFFFFFF82400000)
set(MODULE_BASE_3 0xFFFFFFFF82500000)
set(MODULE_BASE_4 0xFFFFFFFF82600000)
set(MODULE_BASE_5 0xFFFFFFFF82700000)
set(MODULE_BASE_6 0xFFFFFFFF82800000)
set(MODULE_BASE_7 0xFFFFFFFF82900000)

set(MODULE_AXMODS)
set(slot 0)
foreach(dir ${MODULE_DIRS})
    if(IS_DIRECTORY ${dir})
        get_filename_component(name ${dir} NAME)
        file(GLOB mod_srcs CONFIGURE_DEPENDS ${dir}/*.cpp ${dir}/*.S)

        set(base ${MODULE_BASE_${slot}})
        add_executable(mod_${name} ${mod_srcs})
        target_include_directories(mod_${name} PRIVATE ${KERNEL_INCLUDES})
        target_compile_options(mod_${name} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>
            $<$<COMPILE_LANGUAGE:ASM>:-fno-pic;-fno-pie>)
        target_link_options(mod_${name} PRIVATE
            -nostdlib -no-pie -Wl,--build-id=none
            -Wl,-T,${ROOT}/Kernel/linker/module.ld
            -Wl,-defsym,AX_MOD_BASE=${base}
            -Wl,-z,max-page-size=0x1000 -Wl,--no-dynamic-linker)
        set_target_properties(mod_${name} PROPERTIES
            OUTPUT_NAME ${name}
            SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY ${BUILD}/modules
            LINK_DEPENDS ${ROOT}/Kernel/linker/module.ld
            CXX_STANDARD 17 CXX_EXTENSIONS ON)

        set(mod_elf ${BUILD}/modules/${name}.elf)
        set(mod_axmod ${BUILD}/modules/${name}.axmod)

        add_custom_command(
            OUTPUT ${mod_axmod}
            COMMAND ${MKMOD} ${mod_elf} ${mod_axmod} ${name} ${slot}
            DEPENDS mod_${name} ${mod_elf} tools
            COMMENT "Empaquetando ${name}.axmod")

        list(APPEND MODULE_AXMODS ${mod_axmod})
        math(EXPR slot "${slot} + 1")
    endif()
endforeach()
add_custom_target(modules DEPENDS ${MODULE_AXMODS})

# ---------------------------------------------------------------------------
# 6. Biblioteca de usuario (libaxos.a + libaxos.axl)
# ---------------------------------------------------------------------------
file(GLOB LIB_SRCS CONFIGURE_DEPENDS ${ROOT}/apis/src/*.cpp ${ROOT}/apis/src/*.S)
add_library(axos_static STATIC ${LIB_SRCS})
target_include_directories(axos_static PRIVATE ${USER_INCLUDES})
target_compile_options(axos_static PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${USER_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:-fPIE>)
set_target_properties(axos_static PROPERTIES
    OUTPUT_NAME axos
    ARCHIVE_OUTPUT_DIRECTORY ${BUILD}/lib
    CXX_STANDARD 17 CXX_EXTENSIONS ON)

set(LIBAXOS_A ${BUILD}/lib/libaxos.a)
set(LIBAXOS_AXL ${BUILD}/lib/libaxos.axl)
add_custom_command(
    OUTPUT ${LIBAXOS_AXL}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD}/lib
    COMMAND ${MKAXL} ${LIBAXOS_A} ${LIBAXOS_AXL} libaxos
    DEPENDS axos_static ${MKAXL} tools
    COMMENT "Empaquetando libaxos.axl")
add_custom_target(userlib DEPENDS ${LIBAXOS_AXL})

# ---------------------------------------------------------------------------
# 7. Aplicaciones
# ---------------------------------------------------------------------------
file(GLOB APP_SRCS CONFIGURE_DEPENDS ${ROOT}/apps/native/*.cpp)
set(APP_OUTPUTS)
foreach(src ${APP_SRCS})
    get_filename_component(name ${src} NAME_WE)
    add_executable(app_${name} ${src})
    target_include_directories(app_${name} PRIVATE ${USER_INCLUDES})
    target_compile_options(app_${name} PRIVATE ${USER_CXX_FLAGS})
    target_link_options(app_${name} PRIVATE
        -nostdlib -pie -Wl,--build-id=none -Wl,--no-dynamic-linker
        -Wl,-T,${ROOT}/apis/linker/userspace.ld -Wl,--undefined=_start)
    target_link_libraries(app_${name} PRIVATE axos_static)
    set_target_properties(app_${name} PROPERTIES
        OUTPUT_NAME ${name}
        RUNTIME_OUTPUT_DIRECTORY ${BUILD}/apps
        CXX_STANDARD 17 CXX_EXTENSIONS ON)

    set(axe ${BUILD}/apps/${name}.axe)
    add_custom_command(
        OUTPUT ${axe}
        COMMAND ${MKAXE} $<TARGET_FILE:app_${name}> ${axe} ${name}
        DEPENDS app_${name} ${MKAXE} tools
        COMMENT "Empaquetando ${name}.axe")
    list(APPEND APP_OUTPUTS ${axe})
endforeach()

# Aplicaciones bytecode (.ax -> .axb)
file(GLOB AX_SRCS CONFIGURE_DEPENDS ${ROOT}/apps/axolyn/*.ax)
set(AXB_OUTPUTS)
foreach(src ${AX_SRCS})
    get_filename_component(name ${src} NAME_WE)
    set(axb ${BUILD}/apps/${name}.axb)
    add_custom_command(
        OUTPUT ${axb}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD}/apps
        COMMAND $<TARGET_FILE:axc> ${src} -o ${axb}
        DEPENDS axc ${src}
        COMMENT "Compilando ${name}.ax -> .axb")
    list(APPEND AXB_OUTPUTS ${axb})
endforeach()

add_custom_target(apps DEPENDS ${APP_OUTPUTS} ${AXB_OUTPUTS})

# ---------------------------------------------------------------------------
# 8. Initramfs
# ---------------------------------------------------------------------------
set(INITRD ${BUILD}/initrd.axfs)
set(MOTD ${CMAKE_BINARY_DIR}/motd.txt)
file(WRITE ${MOTD}
    "AxolynOS - sistema experimental de microkernel.\nEscribe \"help\" para ver los comandos disponibles.\n")

add_custom_command(
    OUTPUT ${INITRD}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${STAGE}/apps ${STAGE}/modules ${STAGE}/etc
            ${STAGE}/lib ${STAGE}/help ${STAGE}/install
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BUILD}/apps ${STAGE}/apps
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BUILD}/lib ${STAGE}/lib
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BUILD}/modules ${STAGE}/modules
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOT}/apps/help/README.axh ${STAGE}/help/README.axh
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOT}/apps/help/help.axh ${STAGE}/help/help.axh
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOT}/apps/help/shell.axh ${STAGE}/help/shell.axh
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MOTD} ${STAGE}/etc/motd
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STAGE1} ${STAGE2} ${AXOLYN_ELF} ${STAGE}/install/
    COMMAND ${MKINITRAMFS} ${STAGE} ${INITRD}
    DEPENDS apps userlib modules ${STAGE1} ${STAGE2} ${MKINITRAMFS} tools axolyn_kernel
    COMMENT "Generando build/initrd.axfs")
add_custom_target(initrd DEPENDS ${INITRD})

# ---------------------------------------------------------------------------
# 9. Imagen de disco
# ---------------------------------------------------------------------------
set(DISK_IMG ${BUILD}/axolynos.img)
add_custom_command(
    OUTPUT ${DISK_IMG}
    COMMAND ${MKDISK} --stage1 ${STAGE1} --stage2 ${STAGE2}
            --kernel ${AXOLYN_ELF} --initrd ${INITRD} --out ${DISK_IMG}
    DEPENDS ${INITRD} ${MKDISK} ${STAGE1} ${STAGE2} tools axolyn_kernel
    COMMENT "Generando build/axolynos.img")
add_custom_target(image DEPENDS ${DISK_IMG})

# ---------------------------------------------------------------------------
# 10. Cargador UEFI
# ---------------------------------------------------------------------------
set(BOOTX64 ${BUILD}/efi/EFI/BOOT/BOOTX64.EFI)
add_custom_command(
    OUTPUT ${BOOTX64}
    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${ROOT}/Kernel/bootloader/uefi
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${BUILD}/efi/EFI/BOOT ${BUILD}/efi/EFI/AXOLYNOS
    COMMAND ${CMAKE_COMMAND} -E copy
            ${ROOT}/Kernel/bootloader/uefi/BOOTX64.EFI
            ${BUILD}/efi/EFI/BOOT/BOOTX64.EFI
    COMMAND ${CMAKE_COMMAND} -E copy ${AXOLYN_ELF}
            ${BUILD}/efi/EFI/AXOLYNOS/axolyn.elf
    COMMAND ${CMAKE_COMMAND} -E copy ${INITRD}
            ${BUILD}/efi/EFI/AXOLYNOS/initrd.axfs
    DEPENDS ${ROOT}/Kernel/bootloader/uefi/boot.c
            ${ROOT}/tools/elf2efi.py ${INITRD} axolyn_kernel
    COMMENT "Compilando BOOTX64.EFI")
add_custom_target(uefi DEPENDS ${BOOTX64})

# ---------------------------------------------------------------------------
# 11. ISO hibrida BIOS + UEFI
# ---------------------------------------------------------------------------
set(ISO ${BUILD}/AxolynOS.iso)
set(ISO_STAGE ${BUILD}/iso)
set(EFIBOOT ${BUILD}/efiboot.img)

add_custom_command(
    OUTPUT ${ISO}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${ISO_STAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${ISO_STAGE}/EFI/BOOT ${ISO_STAGE}/EFI/AXOLYNOS ${ISO_STAGE}/boot
    COMMAND ${CMAKE_COMMAND} -E copy ${DISK_IMG} ${ISO_STAGE}/axolynos.img
    COMMAND ${CMAKE_COMMAND} -E copy
            ${BUILD}/efi/EFI/BOOT/BOOTX64.EFI ${ISO_STAGE}/EFI/BOOT/BOOTX64.EFI
    COMMAND ${CMAKE_COMMAND} -E copy
            ${BUILD}/efi/EFI/AXOLYNOS/axolyn.elf ${ISO_STAGE}/EFI/AXOLYNOS/axolyn.elf
    COMMAND ${CMAKE_COMMAND} -E copy
            ${BUILD}/efi/EFI/AXOLYNOS/initrd.axfs ${ISO_STAGE}/EFI/AXOLYNOS/initrd.axfs
    COMMAND ${CMAKE_COMMAND} -E rm -f ${EFIBOOT}
    COMMAND ${DD} if=/dev/zero of=${EFIBOOT} bs=1k count=4096 status=none
    COMMAND ${MKFS_VFAT} -F 12 -n AXEFI ${EFIBOOT}
    COMMAND ${MMD} -i ${EFIBOOT} ::/EFI
    COMMAND ${MMD} -i ${EFIBOOT} ::/EFI/BOOT
    COMMAND ${MMD} -i ${EFIBOOT} ::/EFI/AXOLYNOS
    COMMAND ${MCOPY} -i ${EFIBOOT}
            ${BUILD}/efi/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/
    COMMAND ${MCOPY} -i ${EFIBOOT}
            ${BUILD}/efi/EFI/AXOLYNOS/axolyn.elf ::/EFI/AXOLYNOS/
    COMMAND ${MCOPY} -i ${EFIBOOT}
            ${BUILD}/efi/EFI/AXOLYNOS/initrd.axfs ::/EFI/AXOLYNOS/
    COMMAND ${CMAKE_COMMAND} -E copy ${EFIBOOT} ${ISO_STAGE}/boot/efiboot.img
    COMMAND ${CMAKE_COMMAND} -E rm -f ${ISO}
    COMMAND ${XORRISO} -as mkisofs -iso-level 3 -full-iso9660-filenames
            -joliet -rational-rock -volid AXOLYNOS
            -appid "AxolynOS Hybrid BIOS+UEFI"
            -publisher "AxolynOS Project" -preparer "AxolynOS Build"
            -eltorito-boot axolynos.img -hard-disk-boot
            -eltorito-alt-boot -e boot/efiboot.img -no-emul-boot
            -isohybrid-gpt-basdat -append_partition 2 0xEF ${EFIBOOT}
            -o ${ISO} ${ISO_STAGE}
    DEPENDS ${DISK_IMG} ${BOOTX64} ${XORRISO} ${MKFS_VFAT} ${MMD} ${MCOPY}
    COMMENT "Generando build/AxolynOS.iso (BIOS + UEFI)")

add_custom_target(iso DEPENDS ${ISO})
add_custom_target(axos ALL DEPENDS ${ISO})

message(STATUS "AxolynOS: build configurado. Artefactos en ${BUILD}")
