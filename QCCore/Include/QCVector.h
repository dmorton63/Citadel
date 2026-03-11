#pragma once

// QCommon Vector - Dynamic array container
// Namespace: QC

#include "QCTypes.h"

namespace QC
{

    template <typename T>
    class Vector
    {
    public:
        Vector();
        explicit Vector(usize initialCapacity);
        Vector(const Vector &other);
        Vector(Vector &&other) noexcept;
        ~Vector();

        Vector &operator=(const Vector &other);
        Vector &operator=(Vector &&other) noexcept;

        // Element access
        T &operator[](usize index);
        const T &operator[](usize index) const;
        T &at(usize index);
        const T &at(usize index) const;
        T &front();
        T &back();
        T *data();
        const T *data() const;

        // Capacity
        bool empty() const { return m_size == 0; }
        usize size() const { return m_size; }
        usize capacity() const { return m_capacity; }
        void reserve(usize newCapacity);
        void shrink_to_fit();

        // Modifiers
        void clear();
        void push_back(const T &value);
        void push_back(T &&value);
        template <typename... Args>
        T &emplace_back(Args &&...args);
        void pop_back();
        void resize(usize newSize);

        // Iterators
        T *begin() { return m_data; }
        T *end() { return m_data + m_size; }
        const T *begin() const { return m_data; }
        const T *end() const { return m_data + m_size; }

    private:
        void grow();

        T *m_data;
        usize m_size;
        usize m_capacity;
    };

    // Template implementations must be in header

    template <typename T>
    Vector<T>::Vector() : m_data(nullptr), m_size(0), m_capacity(0) {}

    template <typename T>
    Vector<T>::Vector(usize initialCapacity) : m_data(nullptr), m_size(0), m_capacity(0)
    {
        reserve(initialCapacity);
    }

    template <typename T>
    Vector<T>::Vector(const Vector &other) : m_data(nullptr), m_size(0), m_capacity(0)
    {
        reserve(other.m_size);
        for (usize i = 0; i < other.m_size; ++i)
        {
            new (&m_data[i]) T(other.m_data[i]);
        }
        m_size = other.m_size;
    }

    template <typename T>
    Vector<T>::Vector(Vector &&other) noexcept
        : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity)
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    template <typename T>
    Vector<T>::~Vector()
    {
        clear();
        if (m_data)
        {
            operator delete(m_data);
        }
    }

    template <typename T>
    Vector<T> &Vector<T>::operator=(const Vector &other)
    {
        if (this != &other)
        {
            clear();
            reserve(other.m_size);
            for (usize i = 0; i < other.m_size; ++i)
            {
                new (&m_data[i]) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }
        return *this;
    }

    template <typename T>
    Vector<T> &Vector<T>::operator=(Vector &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            if (m_data)
            {
                operator delete(m_data);
            }
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    template <typename T>
    T &Vector<T>::operator[](usize index)
    {
        return m_data[index];
    }

    template <typename T>
    const T &Vector<T>::operator[](usize index) const
    {
        return m_data[index];
    }

    template <typename T>
    T &Vector<T>::at(usize index)
    {
        return m_data[index];
    }

    template <typename T>
    const T &Vector<T>::at(usize index) const
    {
        return m_data[index];
    }

    template <typename T>
    T &Vector<T>::front()
    {
        return m_data[0];
    }

    template <typename T>
    T &Vector<T>::back()
    {
        return m_data[m_size - 1];
    }

    template <typename T>
    T *Vector<T>::data()
    {
        return m_data;
    }

    template <typename T>
    const T *Vector<T>::data() const
    {
        return m_data;
    }

    template <typename T>
    void Vector<T>::reserve(usize newCapacity)
    {
        if (newCapacity <= m_capacity)
            return;

        T *newData = static_cast<T *>(operator new(newCapacity * sizeof(T)));
        for (usize i = 0; i < m_size; ++i)
        {
            new (&newData[i]) T(static_cast<T &&>(m_data[i]));
            m_data[i].~T();
        }
        if (m_data)
        {
            operator delete(m_data);
        }
        m_data = newData;
        m_capacity = newCapacity;
    }

    template <typename T>
    void Vector<T>::shrink_to_fit()
    {
        if (m_size < m_capacity)
        {
            T *newData = nullptr;
            if (m_size > 0)
            {
                newData = static_cast<T *>(operator new(m_size * sizeof(T)));
                for (usize i = 0; i < m_size; ++i)
                {
                    new (&newData[i]) T(static_cast<T &&>(m_data[i]));
                    m_data[i].~T();
                }
            }
            if (m_data)
            {
                operator delete(m_data);
            }
            m_data = newData;
            m_capacity = m_size;
        }
    }

    template <typename T>
    void Vector<T>::clear()
    {
        for (usize i = 0; i < m_size; ++i)
        {
            m_data[i].~T();
        }
        m_size = 0;
    }

    template <typename T>
    void Vector<T>::push_back(const T &value)
    {
        if (m_size >= m_capacity)
        {
            grow();
        }
        new (&m_data[m_size]) T(value);
        ++m_size;
    }

    template <typename T>
    void Vector<T>::push_back(T &&value)
    {
        if (m_size >= m_capacity)
        {
            grow();
        }
        new (&m_data[m_size]) T(static_cast<T &&>(value));
        ++m_size;
    }

    template <typename T>
    template <typename... Args>
    T &Vector<T>::emplace_back(Args &&...args)
    {
        if (m_size >= m_capacity)
        {
            grow();
        }
        new (&m_data[m_size]) T(static_cast<Args &&>(args)...);
        return m_data[m_size++];
    }

    template <typename T>
    void Vector<T>::pop_back()
    {
        if (m_size > 0)
        {
            --m_size;
            m_data[m_size].~T();
        }
    }

    template <typename T>
    void Vector<T>::resize(usize newSize)
    {
        if (newSize > m_capacity)
        {
            reserve(newSize);
        }
        while (m_size < newSize)
        {
            new (&m_data[m_size]) T();
            ++m_size;
        }
        while (m_size > newSize)
        {
            --m_size;
            m_data[m_size].~T();
        }
    }

    template <typename T>
    void Vector<T>::grow()
    {
        usize newCapacity = m_capacity == 0 ? 8 : m_capacity * 2;
        reserve(newCapacity);
    }

} // namespace QC
