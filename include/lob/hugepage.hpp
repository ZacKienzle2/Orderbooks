#ifndef LOB_HUGEPAGE_HPP
#define LOB_HUGEPAGE_HPP

#include <cstddef>
#include <cstdint>
#include <new>

#if defined(__linux__) || defined(__APPLE__)
    #include <sys/mman.h>
#endif
#if defined(__APPLE__)
    #include <mach/vm_statistics.h>
#endif

namespace lob {

// Owns a byte region backed, where the platform allows, by 2 MiB huge pages.
//
// Large slab storage spread over 4 KiB pages thrashes the data TLB during a
// market-data burst: a few thousand resting orders touch hundreds of pages,
// each needing its own TLB entry. A 2 MiB page covers 512 of those 4 KiB
// pages with one entry, so the whole slab fits in a handful of TLB slots and
// a page-walk stops appearing in the tail latency.
//
// The allocation strategy is a fallback chain. On Linux it asks for explicit
// 2 MiB huge pages (MAP_HUGETLB | MAP_HUGE_2MB); if the host reserved no huge
// pages the mapping fails and it drops to a plain anonymous mapping hinted for
// transparent huge pages (MADV_HUGEPAGE). On macOS it asks for a 2 MiB
// superpage, then a plain mapping. Everywhere else, and if every mapping
// fails, it falls back to an over-aligned heap allocation. source() reports
// which backing won so a test or a startup log can confirm the intent held.
//
// The region is never pre-faulted (no MAP_POPULATE). Pages stay unmapped
// until first write, so the thread that first touches the slab is the one the
// kernel binds the pages to under first-touch NUMA policy. This is what lets
// the slab arena keep its lazy, consumer-thread freelist build (ADR-0016)
// while gaining huge-page backing.
class hugepage_region {
  public:
    enum class backing : std::uint8_t { none, huge, mapped, heap };

    static constexpr std::size_t huge_page_bytes = static_cast<std::size_t>(2) << 20;

    explicit hugepage_region(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
        acquire_(bytes, alignment);
    }

    ~hugepage_region() { release_(); }

    hugepage_region(hugepage_region&& other) noexcept
        : ptr_(other.ptr_),
          bytes_(other.bytes_),
          mapped_bytes_(other.mapped_bytes_),
          alignment_(other.alignment_),
          backing_(other.backing_) {
        other.ptr_ = nullptr;
        other.bytes_ = 0;
        other.mapped_bytes_ = 0;
        other.backing_ = backing::none;
    }

    hugepage_region& operator=(hugepage_region&& other) noexcept {
        if (this != &other) {
            release_();
            ptr_ = other.ptr_;
            bytes_ = other.bytes_;
            mapped_bytes_ = other.mapped_bytes_;
            alignment_ = other.alignment_;
            backing_ = other.backing_;
            other.ptr_ = nullptr;
            other.bytes_ = 0;
            other.mapped_bytes_ = 0;
            other.backing_ = backing::none;
        }
        return *this;
    }

    hugepage_region(const hugepage_region&) = delete;
    hugepage_region& operator=(const hugepage_region&) = delete;

    [[nodiscard]] void* data() const noexcept { return ptr_; }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_; }

    [[nodiscard]] backing source() const noexcept { return backing_; }

    [[nodiscard]] bool huge() const noexcept { return backing_ == backing::huge; }

  private:
    static std::size_t round_up_(std::size_t n, std::size_t multiple) noexcept {
        return ((n + multiple - 1) / multiple) * multiple;
    }

    void acquire_(std::size_t bytes, std::size_t alignment) {
        bytes_ = bytes;
        alignment_ = alignment;

#if defined(__linux__) && defined(MAP_HUGETLB)
    #if defined(MAP_HUGE_2MB)
        constexpr int huge_2mb = MAP_HUGE_2MB;
    #else
        constexpr int huge_2mb = 21 << 26;  // 2 MiB selector in the MAP_HUGE bitfield
    #endif
        const std::size_t rounded = round_up_(bytes, huge_page_bytes);
        void* huge = ::mmap(nullptr,
                            rounded,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | huge_2mb,
                            -1,
                            0);
        if (huge != MAP_FAILED) {
            ptr_ = huge;
            mapped_bytes_ = rounded;
            backing_ = backing::huge;
            return;
        }
        void* plain =
            ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (plain != MAP_FAILED) {
    #if defined(MADV_HUGEPAGE)
            ::madvise(plain, bytes, MADV_HUGEPAGE);
    #endif
            ptr_ = plain;
            mapped_bytes_ = bytes;
            backing_ = backing::mapped;
            return;
        }
#elif defined(__APPLE__)
        const std::size_t rounded = round_up_(bytes, huge_page_bytes);
        void* super = ::mmap(nullptr,
                             rounded,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON,
                             VM_FLAGS_SUPERPAGE_SIZE_2MB,
                             0);
        if (super != MAP_FAILED) {
            ptr_ = super;
            mapped_bytes_ = rounded;
            backing_ = backing::huge;
            return;
        }
        void* plain = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (plain != MAP_FAILED) {
            ptr_ = plain;
            mapped_bytes_ = bytes;
            backing_ = backing::mapped;
            return;
        }
#endif
        ptr_ = ::operator new(bytes, std::align_val_t{alignment});
        mapped_bytes_ = 0;
        backing_ = backing::heap;
    }

    void release_() noexcept {
        if (ptr_ == nullptr) {
            return;
        }
#if defined(__linux__) || defined(__APPLE__)
        if (backing_ == backing::huge || backing_ == backing::mapped) {
            ::munmap(ptr_, mapped_bytes_);
            ptr_ = nullptr;
            return;
        }
#endif
        ::operator delete(ptr_, std::align_val_t{alignment_});
        ptr_ = nullptr;
    }

    void* ptr_{nullptr};
    std::size_t bytes_{0};
    std::size_t mapped_bytes_{0};
    std::size_t alignment_{alignof(std::max_align_t)};
    backing backing_{backing::none};
};

}  // namespace lob

#endif  // LOB_HUGEPAGE_HPP
