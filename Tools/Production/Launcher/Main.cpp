#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct HandleCloser {
  void operator()(void *value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
using Handle = std::unique_ptr<void, HandleCloser>;

std::wstring Wide(const std::string &value) {
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0) throw std::runtime_error("Texto UTF-8 no válido en la publicación.");
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
  return result;
}

std::string Utf8(const std::wstring &value) {
  const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
  return result;
}

std::string ReadLine(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::string line;
  if (!std::getline(input, line)) throw std::runtime_error("No existe una publicación de producción válida. Ejecuta .vscode\\build.bat.");
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

std::string Hash(const fs::path &path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    throw std::runtime_error("No se pudo verificar SHA-256.");
  struct AlgorithmGuard { BCRYPT_ALG_HANDLE value; ~AlgorithmGuard() { BCryptCloseAlgorithmProvider(value, 0); } } algorithmGuard{algorithm};
  DWORD objectLength = 0;
  DWORD read = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &read, 0) < 0)
    throw std::runtime_error("No se pudo preparar SHA-256.");
  std::vector<UCHAR> object(objectLength);
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0)
    throw std::runtime_error("No se pudo crear SHA-256.");
  struct HashGuard { BCRYPT_HASH_HANDLE value; ~HashGuard() { BCryptDestroyHash(value); } } hashGuard{hash};
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Falta un archivo de producción: " + Utf8(path.filename().wstring()));
  std::array<char, 65536> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (input.gcount() > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0)
      throw std::runtime_error("No se pudo leer un archivo de producción.");
  }
  if (input.bad()) throw std::runtime_error("No se pudo completar la lectura de producción.");
  std::array<UCHAR, 32> digest{};
  if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
    throw std::runtime_error("No se pudo completar SHA-256.");
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto byte : digest) result << std::setw(2) << static_cast<unsigned>(byte);
  return result.str();
}

void Validate(const fs::path &release) {
  std::ifstream manifest(release / L"release.manifest", std::ios::binary);
  std::string line;
  if (!std::getline(manifest, line) || line != "NO_TRACE_RELEASE_V1")
    throw std::runtime_error("La publicación no tiene un manifiesto compatible.");
  bool executable = false;
  bool configuration = false;
  bool font = false;
  bool actors = false;
  unsigned files = 0;
  while (std::getline(manifest, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto separator = line.find('\t');
    if (separator != 64 || line.size() <= 65) throw std::runtime_error("El manifiesto de producción está incompleto.");
    const std::string expected = line.substr(0, 64);
    if (!std::all_of(expected.begin(), expected.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
      throw std::runtime_error("El manifiesto contiene una suma no válida.");
    const auto name = line.substr(65);
    const fs::path relative(Wide(name));
    if (relative.is_absolute() || relative.has_root_name() || relative.empty())
      throw std::runtime_error("El manifiesto contiene una ruta no válida.");
    for (const auto &component : relative)
      if (component == L".." || component == L".") throw std::runtime_error("La publicación contiene una ruta fuera de su carpeta.");
    if (Hash(release / relative) != expected) throw std::runtime_error("Archivo de producción dañado o modificado: " + name);
    executable |= name == "AwesomeEngine.exe";
    configuration |= name == "Content/Config.json";
    font |= name == "Content/Fonts/Relidux.otf";
    actors |= name == "Content/Textures/NoTrace/Characters/OperativeAtlas.png";
    ++files;
  }
  if (!manifest.eof() || files < 4 || !executable || !configuration || !font || !actors)
    throw std::runtime_error("La publicación no contiene el ejecutable y los recursos obligatorios.");
}

fs::path CurrentRelease(const fs::path &root) {
  const auto id = ReadLine(root / L"production/current.txt");
  if (id.empty() || id == "." || id == ".." || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.';
      })) throw std::runtime_error("La referencia de producción no es válida.");
  return root / L"production" / L"releases" / Wide(id);
}

std::wstring Quote(const std::wstring &argument) {
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (const auto character : argument) {
    if (character == L'\\') { ++slashes; continue; }
    result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (character == L'"') result += L'\\';
    result += character;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

void Report(const fs::path &release, const fs::path &save, DWORD processId, const std::wstring &arguments) {
  std::array<wchar_t, 32768> destination{};
  const DWORD size = GetEnvironmentVariableW(L"NO_TRACE_LAUNCHER_REPORT", destination.data(), static_cast<DWORD>(destination.size()));
  if (!size || size >= destination.size()) return;
  const fs::path output(destination.data());
  const fs::path pending(output.wstring() + L".tmp");
  std::ofstream report(pending, std::ios::binary | std::ios::trunc);
  report << "pid=" << processId << "\nrelease=" << Utf8(release.wstring()) << "\nsave=" << Utf8(save.wstring()) << "\narguments=" << Utf8(arguments) << '\n';
  report.close();
  if (!report) return;
  std::error_code error;
  fs::rename(pending, output, error);
}

void Error(const std::string &message, bool validationOnly) {
  const std::string text = "NO TRACE: " + message + "\r\n";
  DWORD written = 0;
  WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  if (!validationOnly) MessageBoxW(nullptr, Wide(message).c_str(), L"NO TRACE — Producción", MB_OK | MB_ICONERROR);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto *rawArguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!rawArguments) return 1;
  std::vector<std::wstring> arguments(rawArguments, rawArguments + count);
  LocalFree(rawArguments);
  const bool validatePath = count == 3 && arguments[1] == L"--validate-release";
  const bool validateCurrent = count == 2 && arguments[1] == L"--validate-production";
  const bool validationOnly = validatePath || validateCurrent;
  try {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) throw std::runtime_error("No se pudo resolver la carpeta del lanzador.");
    const fs::path root = fs::path(executable.data()).parent_path();
    const fs::path release = validatePath ? fs::absolute(fs::path(arguments[2])) : CurrentRelease(root);
    Validate(release);
    if (validationOnly) return 0;
    const fs::path userData = root / L"production" / L"user-data";
    fs::create_directories(userData);
    const fs::path save = userData / L"Save.json";
    if (!SetEnvironmentVariableW(L"AWESOME_ENGINE_SAVE_FILE", save.c_str()))
      throw std::runtime_error("No se pudo configurar el guardado compartido de producción.");
    const fs::path game = release / L"AwesomeEngine.exe";
    std::wstring forwarded;
    for (int index = 1; index < count; ++index) forwarded += L" " + Quote(arguments[static_cast<std::size_t>(index)]);
    std::wstring command = Quote(game.wstring()) + forwarded;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, release.c_str(), &startup, &process))
      throw std::runtime_error("No se pudo abrir la producción. Código de Windows: " + std::to_string(GetLastError()));
    Handle thread(process.hThread);
    Handle child(process.hProcess);
    Report(release, save, process.dwProcessId, forwarded);
    // Conserva el lanzador abierto para que Windows impida sustituirlo durante la partida.
    WaitForSingleObject(child.get(), INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(child.get(), &result);
    return static_cast<int>(result);
  } catch (const std::exception &exception) {
    Error(exception.what(), validationOnly);
    return 1;
  }
}
