#include "ftb/framing.hpp"
#include <cerrno>
#include <string>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ftb {

    namespace {
        [[noreturn]] void throw_errno(int err, const char* call, const std::filesystem::path& path) {
            throw std::system_error(err, std::generic_category(), std::string(call) + " " + path.string());
        }
    }
    
    MappedFile::MappedFile(const std::filesystem::path& path) {
        int fd = open(path.c_str(), O_RDONLY);
        int err;
        if (fd == -1) {
            err = errno;
            throw_errno(err, "open", path);
        }

        struct stat st;
        int temp = fstat(fd, &st);
        err = errno;
        if (temp == -1) {
            close(fd);
            throw_errno(err, "fstat", path);
        }
        std::size_t size = static_cast<size_t>(st.st_size);
        if (size == 0) {
            close(fd);
            return;
        }

        void* addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        err = errno;
        close(fd);
        if (addr == MAP_FAILED) throw_errno(err, "mmap", path);

        data_ = static_cast<std::uint8_t*>(addr);
        size_ = size;
    }

    MappedFile::~MappedFile() {
        release();
    }

    MappedFile::MappedFile(MappedFile&& other) noexcept : 
        data_(std::exchange(other.data_, nullptr)), 
        size_(std::exchange(other.size_, 0)) {}

    MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            release();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    void MappedFile::release() noexcept {
        if (data_ != nullptr) munmap(const_cast<uint8_t*>(data_), size_);
    }
}