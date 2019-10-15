#ifndef ADT_H
#define ADT_H

#include <QtGlobal>

#include <memory>
#include <stdexcept>
#include <type_traits>

template<typename T,
         std::size_t EmbedArraySize = (64-sizeof(std::size_t)-sizeof(T* const))/sizeof(T),
         typename AllocTy = std::allocator<T>
>
class RunTimeSizeArray{
    static_assert (std::is_pod<T>::value, "Element type of RunTimeSizeArray should be pod" );
public:
    RunTimeSizeArray(std::size_t size, const T& initializer)
        : count(size),
          ptr((size <= EmbedArraySize)? array: (alloc.allocate(size)))
    {
        for(std::size_t i = 0; i < size; ++i){
            ptr[i] = initializer;
        }
    }
    ~RunTimeSizeArray(){
        if(count > EmbedArraySize){
            alloc.deallocate(ptr, count);
        }
    }
    std::size_t size() const{return count;}
    const T& at(int index) const{
        if(Q_UNLIKELY(index < 0 || index >= static_cast<int>(count))){
            throw std::out_of_range("RunTimeSizeArray.at(): bad index");
        }
        return ptr[index];
    }
    T& at(int index){
        if(Q_UNLIKELY(index < 0 || index >= static_cast<int>(count))){
            throw std::out_of_range("RunTimeSizeArray.at(): bad index");
        }
        return ptr[index];
    }
    const T& at(std::size_t index) const{
        if(Q_UNLIKELY(index >= count)){
            throw std::out_of_range("RunTimeSizeArray.at(): bad index");
        }
        return ptr[index];
    }
    T& at(std::size_t index){
        if(Q_UNLIKELY(index >= count)){
            throw std::out_of_range("RunTimeSizeArray.at(): bad index");
        }
        return ptr[index];
    }

private:
    AllocTy alloc;
    const std::size_t count;
    T* const ptr;
    T array[EmbedArraySize];
};

#endif // ADT_H
