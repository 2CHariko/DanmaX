#pragma once
#include <QByteArray>
#include <windows.h>
#include <cstring>

// Structural PE fixture only. Tests inject launch hooks; this file is never run.
inline QByteArray playerExecutableFixture(WORD machine = IMAGE_FILE_MACHINE_AMD64) {
    QByteArray bytes(1024, '\0');
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 128;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = machine; nt.FileHeader.NumberOfSections = 1;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.AddressOfEntryPoint = 0x1000; nt.OptionalHeader.SizeOfImage = 0x2000;
    nt.OptionalHeader.SizeOfHeaders = 512; nt.OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
    IMAGE_SECTION_HEADER section{}; section.VirtualAddress = 0x1000;
    section.SizeOfRawData = 512; section.PointerToRawData = 512; section.Characteristics = IMAGE_SCN_MEM_EXECUTE;
    std::memcpy(bytes.data(), &dos, sizeof(dos));
    std::memcpy(bytes.data() + dos.e_lfanew, &nt, sizeof(nt));
    std::memcpy(bytes.data() + dos.e_lfanew + sizeof(nt), &section, sizeof(section));
    return bytes;
}
