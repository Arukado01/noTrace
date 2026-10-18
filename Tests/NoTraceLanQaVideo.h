#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#endif

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace NoTrace::Qa {
  class VideoRecorder {
  public:
    VideoRecorder(const std::filesystem::path &ffmpegPath, const std::filesystem::path &outputPath, unsigned int width, unsigned int height)
        : size_{width, height} {
      if (width == 0 || height == 0 || width % 2 != 0 || height % 2 != 0 ||
          static_cast<std::size_t>(height) > std::numeric_limits<std::size_t>::max() / width / 4)
        throw std::runtime_error("El video RGBA/yuv420p necesita dimensiones positivas, pares y representables");
      if (!pixels_.resize(size_)) throw std::runtime_error("No se pudo reservar la textura del video LAN");
#ifdef _WIN32
      const auto executable = std::filesystem::absolute(ffmpegPath);
      const auto output = std::filesystem::absolute(outputPath);
      if (!std::filesystem::is_regular_file(executable)) throw std::runtime_error("No existe el ejecutable FFmpeg del video LAN");
      std::filesystem::create_directories(output.parent_path());
      SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
      Handle input;
      HANDLE read = nullptr, write = nullptr;
      if (!CreatePipe(&read, &write, &security, 0)) throw WindowsError("No se pudo crear el pipe del video LAN");
      input.Reset(read); input_.Reset(write);
      if (!SetHandleInformation(input_.value, HANDLE_FLAG_INHERIT, 0)) throw WindowsError("No se pudo aislar el pipe del video LAN");
      const auto logPath = output.wstring() + L".ffmpeg.log";
      Handle log(CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
      if (!log.Valid()) throw WindowsError("No se pudo abrir el registro de FFmpeg");
      AttributeList attributes;
      const std::array handles{input.value, log.value};
      if (!UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, const_cast<HANDLE *>(handles.data()), sizeof(handles), nullptr, nullptr))
        throw WindowsError("No se pudo limitar la herencia de handles de FFmpeg");
      STARTUPINFOEXW startup{};
      startup.StartupInfo.cb = sizeof(startup);
      startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
      startup.StartupInfo.hStdInput = input.value;
      startup.StartupInfo.hStdOutput = log.value;
      startup.StartupInfo.hStdError = log.value;
      startup.lpAttributeList = attributes.value;
      std::wstring command = Quote(executable.wstring()) + L" -hide_banner -loglevel error -y -f rawvideo -pixel_format rgba -video_size " +
          std::to_wstring(width) + L"x" + std::to_wstring(height) +
          L" -framerate 30 -i pipe:0 -an -c:v libx264 -preset ultrafast -crf 20 -pix_fmt yuv420p " + Quote(output.wstring());
      PROCESS_INFORMATION child{};
      // El codificador recibe únicamente sus handles y argumentos, sin shell ni ventana de consola.
      if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &child))
        throw WindowsError("No se pudo iniciar FFmpeg");
      process_.Reset(child.hProcess);
      Handle thread(child.hThread);
#else
      static_cast<void>(ffmpegPath); static_cast<void>(outputPath);
      throw std::runtime_error("La grabación LAN por pipe requiere Windows");
#endif
    }

    ~VideoRecorder() noexcept {
      try { Close(); } catch (...) {}
    }
    VideoRecorder(const VideoRecorder &) = delete;
    VideoRecorder &operator=(const VideoRecorder &) = delete;
    VideoRecorder(VideoRecorder &&) = delete;
    VideoRecorder &operator=(VideoRecorder &&) = delete;

    void Record(sf::RenderWindow &window) {
#ifdef _WIN32
      if (!input_.Valid()) throw std::runtime_error("El video LAN ya está cerrado");
      if (window.getSize() != size_) throw std::runtime_error("La ventana LAN cambió de tamaño durante la grabación");
      // Cada llamada anterior a display produce exactamente un cuadro; la cadencia pertenece al arnés.
      pixels_.update(window);
      const auto image = pixels_.copyToImage();
      const auto *data = image.getPixelsPtr();
      std::size_t remaining = static_cast<std::size_t>(size_.x) * size_.y * 4;
      while (remaining != 0) {
        DWORD written = 0;
        const auto count = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024 * 1024));
        if (!WriteFile(input_.value, data, count, &written, nullptr)) throw WindowsError("FFmpeg rechazó un cuadro RGBA del video LAN");
        if (written == 0) throw std::runtime_error("El pipe de FFmpeg no avanzó al escribir un cuadro");
        data += written; remaining -= written;
      }
#else
      static_cast<void>(window);
      throw std::runtime_error("La grabación LAN por pipe requiere Windows");
#endif
    }

    void Close() {
#ifdef _WIN32
      input_.Reset();
      if (!process_.Valid()) return;
      Handle process(std::exchange(process_.value, nullptr));
      const auto result = WaitForSingleObject(process.value, 30000);
      if (result != WAIT_OBJECT_0) {
        const auto error = GetLastError();
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, 5000);
        if (result == WAIT_TIMEOUT) throw std::runtime_error("FFmpeg no finalizó el video LAN en treinta segundos");
        throw std::runtime_error("No se pudo esperar a FFmpeg: " + std::to_string(error));
      }
      DWORD exitCode = 0;
      if (!GetExitCodeProcess(process.value, &exitCode)) throw WindowsError("No se pudo consultar el resultado de FFmpeg");
      if (exitCode != 0) throw std::runtime_error("FFmpeg terminó con error " + std::to_string(exitCode) + "; consulta el archivo .ffmpeg.log del video");
#endif
    }

  private:
#ifdef _WIN32
    struct Handle {
      HANDLE value = nullptr;
      Handle() = default;
      explicit Handle(HANDLE handle) : value(handle) {}
      ~Handle() { Reset(); }
      Handle(const Handle &) = delete;
      Handle &operator=(const Handle &) = delete;
      bool Valid() const { return value != nullptr && value != INVALID_HANDLE_VALUE; }
      void Reset(HANDLE replacement = nullptr) {
        if (Valid()) CloseHandle(value);
        value = replacement;
      }
    };
    static std::runtime_error WindowsError(const char *message) {
      const auto error = GetLastError();
      return std::runtime_error(std::string(message) + ": " + std::to_string(error));
    }
    struct AttributeList {
      std::vector<std::byte> storage;
      LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;
      AttributeList() {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (size == 0) throw WindowsError("No se pudo dimensionar la lista de handles de FFmpeg");
        storage.resize(size);
        const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) throw WindowsError("No se pudo preparar la lista de handles de FFmpeg");
        value = list;
      }
      ~AttributeList() { if (value) DeleteProcThreadAttributeList(value); }
      AttributeList(const AttributeList &) = delete;
      AttributeList &operator=(const AttributeList &) = delete;
    };
    static std::wstring Quote(const std::wstring &argument) {
      std::wstring result{L'"'};
      std::size_t slashes = 0;
      for (const wchar_t value : argument) {
        if (value == L'\\') { ++slashes; continue; }
        result.append(value == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result.push_back(value); slashes = 0;
      }
      result.append(slashes * 2, L'\\'); result.push_back(L'"');
      return result;
    }
    Handle input_, process_;
#endif
    sf::Vector2u size_;
    sf::Texture pixels_;
  };
}
