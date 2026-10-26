#!/usr/bin/env python3
"""
AxolynOS - elf2efi.py

Convierte el objeto compartido ELF que produce gnu-efi (crt0 + boot.o) en un
ejecutable PE32+ que entiende el firmware UEFI.

Por que existe este script:
  El binutils de Ubuntu no incluye el objetivo BFD "efi-app-x86_64", asi que
  el clasico "objcopy --target=efi-app-x86_64" no funciona y el resultado
  seria un ELF (que ningun firmware UEFI sabe ejecutar).  Este conversor hace
  el trabajo a mano:

    1. Copia los segmentos PT_LOAD del ELF a secciones PE (mismas direcciones
       virtuales).  El segmento que empieza en 0 se descarta porque solo
       contiene .gnu.hash/.eh_frame y su RVA chocaria con las cabeceras PE.
    2. NO aplica las reubicaciones: gnu-efi las resuelve al arrancar desde las
       secciones .dynamic/.rela (crt0-efi-x86_64.S, rutina _relocate).  Por eso
       hay que conservar dichas secciones dentro de los datos copiados.
    3. Apunta el directorio de reubicaciones PE a la seccion .reloc que ya trae
       el script de enlazado de gnu-efi (12 bytes, sin entradas).  Sintetizar
       entradas DIR64 aqui haria que el cargador reubicase la imagen y despues
       crt0 la reubicase otra vez (doble reubicacion, punteros corruptos).
    4. Rellena la cabecera PE32+ con Subsistema 10 (EFI application), calcula
       la suma de comprobacion y deja pila/monticulo a cero, igual que los
       ejecutables que produce gnu-efi.

Uso:
    python3 scripts/elf2efi.py entrada.so salida.efi
"""

import struct
import sys

# --- Constantes ELF -------------------------------------------------------
PT_LOAD = 1
SHT_RELA = 4
SHT_PROGBITS = 1
SHF_ALLOC = 0x2
R_X86_64_RELATIVE = 8

# --- Constantes PE --------------------------------------------------------
IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004
IMAGE_FILE_DEBUG_STRIPPED = 0x0200

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

IMAGE_SUBSYSTEM_EFI_APPLICATION = 10

SECTION_ALIGN = 0x1000
FILE_ALIGN = 0x200


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


class Section:
    def __init__(self, name, vaddr, vmsize, data, characteristics):
        self.name = name
        self.vaddr = vaddr
        self.vmsize = vmsize
        self.data = bytearray(data)
        self.characteristics = characteristics
        self.raw_offset = 0


def read_elf(path):
    """Devuelve (blob, entrada, segmentos, reubicaciones, secciones_elf)."""
    with open(path, "rb") as fh:
        blob = fh.read()

    if blob[:4] != b"\x7fELF" or blob[4] != 2:
        raise SystemExit("elf2efi: no es un ELF64")

    e_entry, e_phoff, e_shoff = struct.unpack_from("<QQQ", blob, 24)
    e_phentsize, e_phnum = struct.unpack_from("<HH", blob, 54)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", blob, 58)

    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", blob, off)
        p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from(
            "<QQQQQ", blob, off + 8)
        if p_type != PT_LOAD or p_memsz == 0:
            continue
        segments.append({
            "offset": p_offset,
            "vaddr": p_vaddr,
            "filesz": p_filesz,
            "memsz": p_memsz,
            "flags": p_flags,
        })

    relocs = []
    elf_sections = {}
    if e_shoff and e_shnum:
        shstr_off = e_shoff + e_shstrndx * e_shentsize
        shstr_offset = struct.unpack_from("<Q", blob, shstr_off + 24)[0]

        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            sh_name, sh_type = struct.unpack_from("<II", blob, off)
            sh_flags = struct.unpack_from("<Q", blob, off + 8)[0]
            sh_addr, sh_offset, sh_size = struct.unpack_from("<QQQ", blob,
                                                             off + 16)
            sh_entsize = struct.unpack_from("<Q", blob, off + 56)[0]

            end = blob.find(b"\x00", shstr_offset + sh_name)
            name = blob[shstr_offset + sh_name:end].decode("ascii", "replace")
            if sh_flags & SHF_ALLOC:
                elf_sections[name] = {
                    "type": sh_type,
                    "addr": sh_addr,
                    "offset": sh_offset,
                    "size": sh_size,
                }

            if sh_type != SHT_RELA:
                continue
            count = sh_size // (sh_entsize or 24)
            for j in range(count):
                roff, rinfo = struct.unpack_from("<QQ", blob,
                                                 sh_offset + j * 24)
                raddend = struct.unpack_from("<q", blob,
                                             sh_offset + j * 24 + 16)[0]
                rtype = rinfo & 0xFFFFFFFF
                relocs.append((roff, rtype, raddend))

    return blob, e_entry, segments, relocs, elf_sections


def build(blob, entry, segments, relocs, elf_sections):
    # --- Secciones PE a partir de los segmentos PT_LOAD -------------------
    sections = []
    for seg in segments:
        if seg["vaddr"] == 0:
            continue
        if seg["flags"] & 0x1:      # PF_X
            name = ".text"
            chars = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                     IMAGE_SCN_MEM_READ)
        elif seg["flags"] & 0x2:    # PF_W
            name = ".data"
            chars = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                     IMAGE_SCN_MEM_WRITE)
        else:
            name = ".rdata"
            chars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ

        data = blob[seg["offset"]:seg["offset"] + seg["filesz"]]
        sections.append(Section(name, seg["vaddr"], seg["memsz"], data, chars))

    if not sections:
        raise SystemExit("elf2efi: el ELF no tiene segmentos PT_LOAD utiles")

    # gnu-efi resuelve las reubicaciones al arrancar; solo comprobamos que
    # todas sean del tipo que su crt0 sabe procesar.
    for roff, rtype, _addend in relocs:
        if rtype != R_X86_64_RELATIVE:
            raise SystemExit(
                "elf2efi: reubicacion no soportada tipo %d en 0x%x"
                % (rtype, roff))

    # --- Directorio de reubicaciones --------------------------------------
    # La seccion .reloc la genera el script de enlazado de gnu-efi y ya viene
    # dentro del segmento de datos.  No se sintetizan entradas DIR64: el
    # cargador UEFI no debe reubicar la imagen (lo hace crt0 desde .rela).
    reloc = elf_sections.get(".reloc")
    if reloc is not None and reloc["size"] >= 8:
        reloc_dir = (reloc["addr"], reloc["size"])
    else:
        reloc_dir = (0, 0)

    num_sections = len(sections)
    opt_header_size = 0xF0
    headers_size = align(0x80 + 4 + 20 + opt_header_size + 40 * num_sections,
                         FILE_ALIGN)

    # --- Repartir los datos de las secciones en el fichero ----------------
    raw_offset = headers_size
    for sec in sections:
        sec.raw_offset = raw_offset
        raw_offset += align(len(sec.data), FILE_ALIGN)

    size_of_image = align(max(s.vaddr + s.vmsize for s in sections),
                          SECTION_ALIGN)
    size_of_code = sum(align(len(s.data), FILE_ALIGN) for s in sections
                       if s.characteristics & IMAGE_SCN_CNT_CODE)
    size_of_init = sum(align(len(s.data), FILE_ALIGN) for s in sections
                       if not (s.characteristics & IMAGE_SCN_CNT_CODE))

    out = bytearray(raw_offset)

    # --- Cabecera DOS -----------------------------------------------------
    out[0:2] = b"MZ"
    struct.pack_into("<H", out, 2, 0x90)      # bytes en la ultima pagina
    struct.pack_into("<H", out, 4, 3)         # paginas
    struct.pack_into("<I", out, 0x3C, 0x80)   # e_lfanew
    stub = b"Este programa necesita un firmware UEFI.\r\n$"
    out[0x40:0x40 + len(stub)] = stub

    # --- Firma PE + cabecera COFF ----------------------------------------
    off = 0x80
    out[off:off + 4] = b"PE\x00\x00"
    off += 4

    characteristics = (IMAGE_FILE_EXECUTABLE_IMAGE |
                       IMAGE_FILE_LINE_NUMS_STRIPPED |
                       IMAGE_FILE_DEBUG_STRIPPED)
    struct.pack_into("<HHIIIHH", out, off,
                     0x8664,              # Machine x86-64
                     num_sections,
                     0,                   # TimeDateStamp
                     0,                   # PointerToSymbolTable
                     0,                   # NumberOfSymbols
                     opt_header_size,
                     characteristics)
    off += 20

    # --- Cabecera opcional PE32+ -----------------------------------------
    opt_start = off
    struct.pack_into("<H", out, off, 0x20B)    # PE32+
    struct.pack_into("<BB", out, off + 2, 0, 0)
    struct.pack_into("<I", out, off + 4, size_of_code)
    struct.pack_into("<I", out, off + 8, size_of_init)
    struct.pack_into("<I", out, off + 12, 0)
    struct.pack_into("<I", out, off + 16, entry)          # AddressOfEntryPoint
    struct.pack_into("<I", out, off + 20, sections[0].vaddr)  # BaseOfCode
    struct.pack_into("<Q", out, off + 24, 0)              # ImageBase
    struct.pack_into("<I", out, off + 32, SECTION_ALIGN)
    struct.pack_into("<I", out, off + 36, FILE_ALIGN)
    struct.pack_into("<HHHHHH", out, off + 40, 0, 0, 0, 0, 0, 0)
    struct.pack_into("<I", out, off + 52, 0)              # Win32Version
    struct.pack_into("<I", out, off + 56, size_of_image)
    struct.pack_into("<I", out, off + 60, headers_size)
    struct.pack_into("<I", out, off + 64, 0)              # CheckSum
    struct.pack_into("<H", out, off + 68, IMAGE_SUBSYSTEM_EFI_APPLICATION)
    struct.pack_into("<H", out, off + 70, 0)              # DllCharacteristics
    struct.pack_into("<QQQQ", out, off + 72, 0, 0, 0, 0)  # pila y monticulo
    struct.pack_into("<I", out, off + 104, 0)             # LoaderFlags
    struct.pack_into("<I", out, off + 108, 16)            # NumberOfRvaAndSizes

    dirs_off = off + 112
    for i in range(16):
        struct.pack_into("<II", out, dirs_off + i * 8, 0, 0)
    # DataDirectory[5] = tabla de reubicaciones base
    struct.pack_into("<II", out, dirs_off + 5 * 8, *reloc_dir)

    # --- Tabla de secciones ----------------------------------------------
    off = opt_start + opt_header_size
    for sec in sections:
        name = sec.name.encode("ascii")[:8].ljust(8, b"\x00")
        struct.pack_into("<8sIIIIIIHHI", out, off,
                         name,
                         sec.vmsize,
                         sec.vaddr,
                         align(len(sec.data), FILE_ALIGN),
                         sec.raw_offset,
                         0, 0, 0, 0,
                         sec.characteristics)
        off += 40

    # --- Datos de las secciones ------------------------------------------
    for sec in sections:
        start = sec.raw_offset
        out[start:start + len(sec.data)] = sec.data

    # --- Suma de comprobacion PE -----------------------------------------
    # Algunos firmwares (y VirtualBox) la validan; se calcula con el campo a 0.
    checksum = 0
    for i in range(0, len(out) - 1, 2):
        checksum += struct.unpack_from("<H", out, i)[0]
        checksum = (checksum & 0xFFFF) + (checksum >> 16)
    if len(out) & 1:
        checksum += out[-1]
    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum += len(out)
    struct.pack_into("<I", out, opt_start + 64, checksum & 0xFFFFFFFF)

    return bytes(out)


def main():
    if len(sys.argv) != 3:
        print("uso: elf2efi.py entrada.so salida.efi", file=sys.stderr)
        return 1
    blob, entry, segments, relocs, elf_sections = read_elf(sys.argv[1])
    image = build(blob, entry, segments, relocs, elf_sections)
    with open(sys.argv[2], "wb") as fh:
        fh.write(image)
    print("elf2efi: %s -> %s (%d bytes, %d reubicaciones ELF resueltas por crt0)"
          % (sys.argv[1], sys.argv[2], len(image), len(relocs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
