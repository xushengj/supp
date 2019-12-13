#ifndef ADT_H
#define ADT_H

#include <QtGlobal>

#include <deque>
#include <memory>
#include <stdexcept>
#include <type_traits>

// a run time sized array that can take both int and std::size_t index
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


// a stack that both provides int and std::size_t at() and stack interface (top(), push(), pop())
template<typename T>
class Stack: public std::deque<T>
{
public:
    T& at(int index){
        return std::deque<T>::at(static_cast<std::size_t>(index));
    }
    const T& at(int index)const{
        return std::deque<T>::at(static_cast<std::size_t>(index));
    }
    T& top(){
        return std::deque<T>::back();
    }
    const T& top() const{
        return std::deque<T>::back();
    }
    void push(const T& v){
        std::deque<T>::push_back(v);
    }
    void pop(){
        std::deque<T>::pop_back();
    }
};

#endif // ADT_H
