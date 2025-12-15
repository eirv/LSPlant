module;

#include <cstdint>
#include <type_traits>

#ifdef LSPLANT_USE_MODULES
export module lsplant:handle;
#endif

namespace lsplant::art {

class ArtMethod;

export template <typename MirrorType>
class ObjPtr {
public:
    ObjPtr(MirrorType *reference) : reference_(reference) {}

    inline MirrorType *operator->() const { return Ptr(); }

    inline MirrorType *Ptr() const { return reference_; }

    inline operator MirrorType *() const { return Ptr(); }

private:
    MirrorType *reference_;
};

export template <bool kPoisonReferences, class MirrorType>
class alignas(4) [[gnu::packed]] ObjectReference {
    static MirrorType *Decompress(uint32_t ref) {
        uintptr_t as_bits = kPoisonReferences ? -ref : ref;
        return reinterpret_cast<MirrorType *>(as_bits);
    }

    uint32_t reference_;

public:
    MirrorType *AsMirrorPtr() const { return Decompress(reference_); }
};

export template <class MirrorType>
class alignas(4) [[gnu::packed]] CompressedReference : public ObjectReference<false, MirrorType> {};

export template <class MirrorType>
class alignas(4) [[gnu::packed]] StackReference : public CompressedReference<MirrorType> {};

template <typename To, typename From>  // use like this: down_cast<T*>(foo);
inline To down_cast(From *f) {         // so we only accept pointers
    static_assert(std::is_base_of_v<From, std::remove_pointer_t<To>>,
                  "down_cast unsafe as To is not a subtype of From");

    return static_cast<To>(f);
}

export class ValueObject {};

export template <class ReflectiveType>
class ReflectiveReference {
public:
    static_assert(std::is_same_v<ReflectiveType, ArtMethod>, "Unknown type!");

    ReflectiveType *Ptr() { return val_; }

    void Assign(ReflectiveType *r) { val_ = r; }

private:
    ReflectiveType *val_;
};

export template <typename T>
class ReflectiveHandle : public ValueObject {
public:
    static_assert(std::is_same_v<T, ArtMethod>, "Expected ArtField or ArtMethod");

    T *Get() { return reference_->Ptr(); }

    void Set(T *val) { reference_->Assign(val); }

protected:
    ReflectiveReference<T> *reference_;
};

export template <typename T>
class Handle : public ValueObject {
public:
    Handle(const Handle<T> &handle) : reference_(handle.reference_) {}

    Handle<T> &operator=(const Handle<T> &handle) {
        reference_ = handle.reference_;
        return *this;
    }
    // static_assert(std::is_same_v<T, mirror::Class>, "Expected mirror::Class");

    auto operator->() { return Get(); }

    T *Get() { return down_cast<T *>(reference_->AsMirrorPtr()); }

protected:
    StackReference<T> *reference_;
};

// static_assert(!std::is_trivially_copyable_v<Handle<mirror::Class>>);

// https://cs.android.com/android/_/android/platform/art/+/38cea84b362a10859580e788e984324f36272817
export template <typename T>
class TrivialHandle : public ValueObject {
public:
    // static_assert(std::is_same_v<T, mirror::Class>, "Expected mirror::Class");

    auto operator->() { return Get(); }

    T *Get() { return down_cast<T *>(reference_->AsMirrorPtr()); }

protected:
    StackReference<T> *reference_;
};
}  // namespace lsplant::art
