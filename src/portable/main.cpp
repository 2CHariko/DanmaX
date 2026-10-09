// Single-file distribution; Qt stays dynamically linked in a private portable directory.
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <cstdio>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fdi.h>

namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (!length) throw std::runtime_error("Invalid UTF-8 path.");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), length);
    return result;
}

std::span<const unsigned char> resource(int id) {
    const auto module = GetModuleHandleW(nullptr);
    const auto found = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    const auto loaded = found ? LoadResource(module, found) : nullptr;
    const auto bytes = loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    if (!bytes) throw std::runtime_error("Embedded portable payload is missing.");
    return {bytes, SizeofResource(module, found)};
}

class Sha256 {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> object_;
public:
    Sha256() {
        DWORD size = 0, returned = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Cannot initialize SHA-256.");
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                              sizeof(size), &returned, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            throw std::runtime_error("Cannot initialize SHA-256 storage.");
        }
        object_.resize(size);
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), size, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            throw std::runtime_error("Cannot initialize SHA-256 hash.");
        }
    }
    ~Sha256() { BCryptDestroyHash(hash_); BCryptCloseAlgorithmProvider(algorithm_, 0); }
    void append(std::span<const unsigned char> bytes) {
        if (bytes.size() > std::numeric_limits<ULONG>::max() ||
            BCryptHashData(hash_, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) < 0)
            throw std::runtime_error("SHA-256 update failed.");
    }
    std::string finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("SHA-256 finalization failed.");
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
        return result;
    }
};

std::string hashBytes(std::span<const unsigned char> bytes) {
    Sha256 hash;
    hash.append(bytes);
    return hash.finish();
}

std::string hashFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read a portable runtime file.");
    Sha256 hash;
    std::array<unsigned char, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        hash.append({buffer.data(), static_cast<size_t>(input.gcount())});
    }
    if (!input.eof()) throw std::runtime_error("Cannot read a portable runtime file completely.");
    return hash.finish();
}

struct FileInfo { std::string hash; std::uintmax_t size; };
using Manifest = std::map<std::string, FileInfo>;

fs::path relativePath(const std::string& text) {
    const fs::path path(wide(text));
    if (path.empty() || path.is_absolute() || path.has_root_path() || text.find(':') != std::string::npos)
        throw std::runtime_error("Unsafe portable payload path.");
    for (const auto& component : path) {
        const auto part = component.wstring();
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
            throw std::runtime_error("Unsafe portable payload path component.");
    }
    return path;
}

Manifest readManifest() {
    const auto bytes = resource(102);
    std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    Manifest result;
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find('\t'), second = line.find('\t', first + 1);
        if (first != 64 || second == std::string::npos)
            throw std::runtime_error("Invalid portable file manifest.");
        const auto name = line.substr(second + 1);
        relativePath(name);
        if (!result.emplace(name, FileInfo{line.substr(0, first),
                                          std::stoull(line.substr(first + 1, second - first - 1))}).second)
            throw std::runtime_error("Duplicate portable payload path.");
    }
    if (!result.contains("DanmaX.exe")) throw std::runtime_error("Application missing from payload.");
    return result;
}

// Reject junctions/symlinks before writing or loading executable files.
void ensureDirectory(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path)) {
        current /= part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(attributes & FILE_ATTRIBUTE_DIRECTORY)))
            throw std::runtime_error("Portable directory contains a junction, symlink or regular file.");
        if (attributes == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(current.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Portable directory is not writable. Move the EXE to a writable folder.");
    }
}

bool validRuntime(const fs::path& directory, const Manifest& manifest) {
    if (!fs::is_directory(directory)) return false;
    ensureDirectory(directory);
    for (const auto& [name, info] : manifest) {
        const auto file = directory / relativePath(name);
        ensureDirectory(file.parent_path());
        const DWORD attributes = GetFileAttributesW(file.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            fs::file_size(file) != info.size || hashFile(file) != info.hash) return false;
    }
    // Also reject injected files and reparse directories inside the runtime.
    size_t count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) return false;
        if (!entry.is_directory()) ++count;
    }
    return count == manifest.size();
}

struct CabinetFile {
    std::span<const unsigned char> memory;
    size_t offset = 0;
    HANDLE output = INVALID_HANDLE_VALUE;
    ~CabinetFile() { if (output != INVALID_HANDLE_VALUE) CloseHandle(output); }
};
std::span<const unsigned char> cabinetBytes;
void* DIAMONDAPI allocate(ULONG size) { return std::malloc(size); }
void DIAMONDAPI release(void* pointer) { std::free(pointer); }
INT_PTR DIAMONDAPI openCabinet(char*, int, int) {
    auto* file = new (std::nothrow) CabinetFile{cabinetBytes};
    return file ? reinterpret_cast<INT_PTR>(file) : -1;
}
UINT DIAMONDAPI readCabinet(INT_PTR handle, void* buffer, UINT size) {
    auto& file = *reinterpret_cast<CabinetFile*>(handle);
    const size_t length = std::min<size_t>(size, file.memory.size() - file.offset);
    std::memcpy(buffer, file.memory.data() + file.offset, length);
    file.offset += length;
    return static_cast<UINT>(length);
}
UINT DIAMONDAPI writeFile(INT_PTR handle, void* buffer, UINT size) {
    DWORD written = 0;
    auto& file = *reinterpret_cast<CabinetFile*>(handle);
    return WriteFile(file.output, buffer, size, &written, nullptr) ? written : static_cast<UINT>(-1);
}
int DIAMONDAPI closeFile(INT_PTR handle) { delete reinterpret_cast<CabinetFile*>(handle); return 0; }
long DIAMONDAPI seekCabinet(INT_PTR handle, long distance, int origin) {
    auto& file = *reinterpret_cast<CabinetFile*>(handle);
    const auto base = origin == SEEK_SET ? 0LL : origin == SEEK_CUR ? static_cast<long long>(file.offset)
                                                                            : static_cast<long long>(file.memory.size());
    const auto position = base + distance;
    if (position < 0 || position > static_cast<long long>(file.memory.size()) || position > LONG_MAX) return -1;
    file.offset = static_cast<size_t>(position);
    return static_cast<long>(position);
}
struct Extraction { fs::path directory; const Manifest& manifest; std::string error; };
INT_PTR DIAMONDAPI notification(FDINOTIFICATIONTYPE type, PFDINOTIFICATION note) {
    auto& context = *static_cast<Extraction*>(note->pv);
    try {
        if (type == fdintCOPY_FILE) {
            const auto found = context.manifest.find(note->psz1);
            if (found == context.manifest.end() || static_cast<std::uintmax_t>(note->cb) != found->second.size)
                throw std::runtime_error("Cabinet does not match its file manifest.");
            const auto path = context.directory / relativePath(note->psz1);
            ensureDirectory(path.parent_path());
            auto file = std::make_unique<CabinetFile>();
            file->output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file->output == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot extract portable file.");
            return reinterpret_cast<INT_PTR>(file.release());
        }
        if (type == fdintCLOSE_FILE_INFO) { closeFile(note->hf); return TRUE; }
        if (type == fdintNEXT_CABINET) throw std::runtime_error("Multiple cabinets are not supported.");
        return 0;
    } catch (const std::exception& error) { context.error = error.what(); return -1; }
}

void extract(const fs::path& directory, const Manifest& manifest) {
    ensureDirectory(directory);
    cabinetBytes = resource(101);
    ERF error{};
    const auto decoder = FDICreate(allocate, release, openCabinet, readCabinet, writeFile,
                                   closeFile, seekCabinet, cpuUNKNOWN, &error);
    if (!decoder) throw std::runtime_error("Cannot initialize Windows cabinet decompressor.");
    Extraction context{directory, manifest, {}};
    char name[] = "payload.cab", path[] = "";
    const bool success = FDICopy(decoder, name, path, 0, notification, nullptr, &context) != FALSE;
    FDIDestroy(decoder);
    if (!success) throw std::runtime_error(context.error.empty() ? "Portable cabinet extraction failed." : context.error);
    if (!validRuntime(directory, manifest)) throw std::runtime_error("Extracted runtime failed SHA-256 verification.");
}

std::wstring quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

int launch() {
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) throw std::runtime_error("Cannot locate portable EXE.");
    const fs::path parent = fs::path(std::wstring(executable.data(), length)).parent_path();
    const fs::path data = parent / L"data", runtimeBase = data / L"runtime";
    ensureDirectory(runtimeBase);
    const auto manifest = readManifest();
    // A short directory ID avoids exhausting Win32 DLL path limits in nested portable folders.
    // Integrity is still checked using full SHA-256 hashes from the embedded file manifest.
    const auto key = hashBytes(resource(101)).substr(0, 24);
    const auto runtime = runtimeBase / wide(key);
    const auto parentText = parent.wstring();
    const auto mutexKey = hashBytes({reinterpret_cast<const unsigned char*>(parentText.data()), parentText.size() * sizeof(wchar_t)});
    Handle mutex(CreateMutexW(nullptr, FALSE, (L"Local\\DanmaXPortable-" + wide(mutexKey)).c_str()));
    if (!mutex.value) throw std::runtime_error("Cannot create portable extraction lock.");
    const auto wait = WaitForSingleObject(mutex.value, 60000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        throw std::runtime_error("Another copy is preparing this portable runtime. Try again shortly.");
    try {
        if (!validRuntime(runtime, manifest)) {
            const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
            const auto pending = runtimeBase / (L"extract-" + suffix);
            extract(pending, manifest);
            if (fs::exists(runtime)) fs::rename(runtime, runtimeBase / (wide(key) + L".invalid-" + suffix));
            fs::rename(pending, runtime);
        }
    } catch (...) { ReleaseMutex(mutex.value); throw; }
    ReleaseMutex(mutex.value);

    // Do not inherit SDK/plugin paths. All caches stay beside the portable executable.
    ensureDirectory(data / L"cache" / L"tmp");
    for (const auto* name : {L"QTDIR", L"QT_PLUGIN_PATH", L"QT_QPA_PLATFORM_PLUGIN_PATH", L"QML_IMPORT_PATH", L"QML2_IMPORT_PATH"})
        SetEnvironmentVariableW(name, nullptr);
    SetEnvironmentVariableW(L"PATH", (runtime.wstring() + L";" + [] {
        std::array<wchar_t, MAX_PATH> system{}; GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
        return std::wstring(system.data());
    }()).c_str());
    SetEnvironmentVariableW(L"TEMP", (data / L"cache" / L"tmp").c_str());
    SetEnvironmentVariableW(L"TMP", (data / L"cache" / L"tmp").c_str());
    SetEnvironmentVariableW(L"QML_DISK_CACHE_PATH", (data / L"cache" / L"qml").c_str());
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) throw std::runtime_error("Cannot read portable launch arguments.");
    std::wstring command = quote((runtime / L"DanmaX.exe").wstring()) + L" --data-dir " + quote(data.wstring())
                        + L" --cache-dir " + quote((data / L"cache" / L"qml").wstring());
    for (int index = 1; index < count; ++index) command += L" " + quote(arguments[index]);
    LocalFree(arguments);
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    const auto outputHandle = GetStdHandle(STD_OUTPUT_HANDLE), errorHandle = GetStdHandle(STD_ERROR_HANDLE);
    const bool redirected = (outputHandle && outputHandle != INVALID_HANDLE_VALUE) ||
                            (errorHandle && errorHandle != INVALID_HANDLE_VALUE);
    if (redirected) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = outputHandle;
        startup.hStdError = errorHandle;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW((runtime / L"DanmaX.exe").c_str(), command.data(), nullptr, nullptr,
                         redirected ? TRUE : FALSE, 0, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("Cannot start application. The official VC++ x64 runtime is still required.");
    Handle child(process.hProcess), thread(process.hThread);
    WaitForSingleObject(child.value, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(child.value, &code);
    return static_cast<int>(code);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try { return launch(); }
    catch (const std::exception& error) {
        const auto message = wide(error.what());
        int count = 0;
        auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        bool smoke = false;
        if (arguments) {
            for (int index = 1; index < count; ++index)
                smoke = smoke || std::wstring(arguments[index]) == L"--smoke-test";
            LocalFree(arguments);
        }
        std::fprintf(stderr, "%s\n", error.what());
        if (!smoke) MessageBoxW(nullptr, message.c_str(), L"DanmaX Portable", MB_OK | MB_ICONERROR);
        return 1;
    }
}
