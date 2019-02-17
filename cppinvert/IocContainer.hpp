#pragma once

#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <boost/any.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/exception/all.hpp>
#include <boost/format.hpp>
#include <boost/throw_exception.hpp>

namespace cppinvert
{

typedef boost::error_info<struct tag_errmsg, std::string> StringInfo;

/// A custom exception, so it's easier to track exceptions that are due to errors from the
/// IOC container
class IocException : virtual public boost::exception, virtual public std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "Library threw an exception";
    }
};

/// Used to disambiguate a value that is meant to be handled as a value that will be
/// copied or moved
/// @tparam T The type in the value wrapper
template <class T>
struct value_wrapper
{
    /// Constructor which moves the value to a local member, to be moved later
    value_wrapper(T val)
        : value_(std::move(val))
    {
    }

    /// Method that moves the internal member
    T&& move()
    {
        return std::move(value_);
    }

private:
    T value_;
};

/// Helper for value wrappers similar to ref for reference_wrappers
/// @tparam T The type in the value wrapper
/// @returns The returned value wrapper
template <class T>
inline value_wrapper<T> val(T value)
{
    return value_wrapper<T>(std::move(value));
}

/// Helper for moveable value wrappers similar to ref for reference_wrappers
/// @tparam T The type in the value wrapper
/// @returns The returned value wrapper
template <class T>
inline value_wrapper<T> mval(T& value)
{
    return val(std::move(value));
}

template <class T>
struct is_reference_wrapper : std::false_type
{
};

template <class T>
struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type
{
};

template <class T>
inline constexpr bool is_reference_wrapper_v = is_reference_wrapper<T>::value;

/// @brief Implementation of an IOC container for C++ code
///
/// A container that supports holding any type of object, as well as managing the
/// specified lifetime. In addition, it can create objects if you register the appropriate
/// factory with it. Note: The IOC container is also thread-safe
class IocContainer : private boost::noncopyable
{
public:
    /// Definition for a shared factory function for creating objects
    template <class T, class... TArgs>
    using SharedFactory = std::function<std::shared_ptr<T>(TArgs...)>;

    /// Definition for a factory function for creating objects
    template <class T, class... TArgs>
    using Factory = std::function<std::unique_ptr<T>(TArgs...)>;

    /// Creates the IOC container and also defaults to registering a factory of an
    /// IOC container, so that sub-containers may be created upon request
    IocContainer()
        : m_parent(nullptr)
        , m_registeredFactories()
        , m_registeredInstances()
        , m_mutex()
    {
        // By default, bind a factory any time an IOC container is requested
        Factory<IocContainer> factoryFunc = [this]() {
            // Reference parent for factories
            auto container{std::make_unique<IocContainer>()};
            container->m_parent = this;

            return container;
        };

        registerFactory<IocContainer>(factoryFunc);
    }

    /// Move constructor
    /// @param other The IOC container to take resources from
    IocContainer(IocContainer&& other)
        : m_parent(std::move(other.m_parent))
        , m_registeredFactories(std::move(other.m_registeredFactories))
        , m_registeredInstances(std::move(other.m_registeredInstances))
        , m_mutex()
    {
    }

    /// Destroys the IOC container
    ~IocContainer()
    {
    }

    IocContainer& operator=(IocContainer&& other)
    {
        if (this == &other)
        {
            return *this;
        }

        m_parent = std::move(other.m_parent);
        m_registeredFactories = std::move(other.m_registeredFactories);
        m_registeredInstances = std::move(other.m_registeredInstances);

        return *this;
    }

    /// Return the size of the container. In this context, the size means the number of
    /// instances that are held in the container
    /// @param[in] recursive Provides a mechanism for counting the number of instances in
    /// all subcontainers, as well
    /// @returns The calculated size
    /// NOTE: This calculation has a caveat. While the parent IOC container is locked, if
    /// you are doing
    ///     the recursive case, those will only be locked when they individually count
    ///     their size, and so on, so it's possible that the count will not be an exact
    ///     snapshot for that moment in time
    std::size_t size [[nodiscard]] (bool recursive = false) const
    {
        Lock lock(m_mutex);

        std::size_t size = 0;

        for (const auto& item : m_registeredInstances)
        {
            size += item.second.size();
        }

        if (recursive)
        {
            const char* typeName = getType(*this);

            if (m_registeredInstances.count(typeName))
            {
                auto& innerMap = m_registeredInstances.at(typeName);

                for (const auto& mapPair : innerMap)
                {
                    auto item = boost::any_cast<HolderPtr<IocContainer>>(mapPair.second);
                    size += item->size(recursive);
                }
            }
        }

        return size;
    }

    /// Registers a default factory function for a given type. It implicitly does new T()
    /// to create the type
    /// @tparam T The type of the instance that will be created
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& registerDefaultFactory()
    {
        return registerDefaultFactory<T, T>();
    }

    /// Registers a default factory function for a given type. It implicitly does new T()
    /// to create the type
    /// @tparam T The type of the instance that will be created
    /// @tparam TConcrete The type of the concrete instance that will be created
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T, class TConcrete, class... TArgs>
    IocContainer& registerDefaultFactory()
    {
        Factory<T, TArgs...> factory = [](TArgs... args) {
            return std::make_unique<TConcrete>(args...);
        };

        return registerFactory<T>(factory);
    }

    /// Registers a factory function for a given type
    /// @tparam T The type of the instance that will be created
    /// @param[in] factory The factory function to create the given type
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T, class TFactory>
    IocContainer& registerFactory(TFactory factory)
    {
        Lock lock(m_mutex);

        const char* typeName = getType<T>();
        m_registeredFactories.insert(std::make_pair(typeName, factory));
        return *this;
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    std::enable_if_t<!is_reference_wrapper_v<T>, IocContainer&> bindInstance(
        value_wrapper<T> instance)
    {
        return bindInstance<T>("", std::move(instance));
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    std::enable_if_t<!is_reference_wrapper_v<T>, IocContainer&> bindValue(T instance)
    {
        return bindValue<T>("", std::move(instance));
    }

    /// Registers an instance for a given type. This version refers to the object and will
    /// not manage lifetime
    ///     as the shared_ptr will have a null deleter. To call this, use:
    ///     bindInstance(std::ref(instance)) or bindInstance(std:::cref(instance))
    /// @tparam T The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(std::reference_wrapper<T> instance)
    {
        return bindInstance<T>("", std::move(instance));
    }

    /// Registers an instance for a given type. This version refers to the object and will
    /// not manage lifetime
    ///     as the shared_ptr will have a null deleter. To call this, use:
    ///     bindInstance(std::ref(instance)) or bindInstance(std:::cref(instance))
    /// @tparam T The type of the instance
    /// @tparam T2 The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T, class T2 = T>
    std::enable_if_t<!std::is_same_v<T, T2>, IocContainer&> bindInstance(
        std::reference_wrapper<T2> instance)
    {
        return bindInstance<T>("", instance);
    }

    /// Registers an instance for a given type. This version refers to a pointer to be
    /// held within the container
    ///     and does not manage lifetime. If you want to manage lifetime, please see the
    ///     unique_ptr or shared_ptr versions
    /// @tparam T The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(T* instance)
    {
        return bindInstance<T>("", instance);
    }

    /// Registers an instance for a given type. This version will take in a unique_ptr,
    /// take ownership and manage
    ///     lifetime via a Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(std::unique_ptr<T> instance)
    {
        return bindInstance<T>("", instance);
    }

    /// Registers an instance for a given type. This version will take in a shared_ptr and
    /// share lifetime with
    //      any other shared_ptrs that reference it
    /// @tparam T The type of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(std::shared_ptr<T> instance)
    {
        return bindInstance<T>("", instance);
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    std::enable_if_t<!is_reference_wrapper_v<T>, IocContainer&> bindInstance(
        const std::string& name, value_wrapper<T> instance)
    {
        return bindValue(name, instance.move());
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    std::enable_if_t<!is_reference_wrapper_v<T>, IocContainer&> bindValue(
        const std::string& name, const T& instance)
    {
        return bindInstance<T>(name, std::make_shared<T>(instance));
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(const std::string& name,
                               std::reference_wrapper<T> instance)
    {
        return bindInstance<T>(name, HolderPtr<T>(&instance.get(), [](T*) {}));
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @tparam T2 The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T, class T2>
    std::enable_if_t<!std::is_same_v<T, T2>, IocContainer&> bindInstance(
        const std::string& name, std::reference_wrapper<T2> instance)
    {
        return bindInstance<T>(name, HolderPtr<T>(&instance.get(), [](T*) {}));
    }

    /// Registers an instance for a given type. This version performs a copy of the
    /// object, using the copy
    ///     constructor and will manage lifetime via the Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(const std::string& name, T* instance)
    {
        return bindInstance<T>(name, HolderPtr<T>(instance, [](T*) {}));
    }

    /// Registers an instance for a given type. This version will take in a unique_ptr,
    /// take ownership and manage
    ///     lifetime via a Holder (shared_ptr)
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(const std::string& name, std::unique_ptr<T> instance)
    {
        return bindInstance<T>(name, std::shared_ptr<T>(std::move(instance)));
    }

    /// Registers an instance for a given type. This version will take in a shared_ptr and
    /// share lifetime with any other shared_ptrs that reference it
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @param[in] instance The instance to be held within the container
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& bindInstance(const std::string& name, std::shared_ptr<T> instance)
    {
        Lock lock(m_mutex);

        const char* typeName = getType<T>();
        auto& innerMap = m_registeredInstances[typeName];
        innerMap.insert({name, Holder(instance)});
        return *this;
    }

    /// Utility method to erase an existing instance from the container
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& eraseInstance()
    {
        return eraseInstance<T>("");
    }

    /// Utility method to erase an existing instance from the container
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    template <class T>
    IocContainer& eraseInstance(const std::string& name)
    {
        Lock lock(m_mutex);

        const char* typeName = getType<T>();
        auto iter = m_registeredInstances.find(typeName);

        if (iter != m_registeredInstances.end())
        {
            auto innerIter = iter->second.find(name);
            if (innerIter != iter->second.end())
            {
                iter->second.erase(innerIter);

                // If we have no elements left, we might as well
                // clean up by also removing the outer container
                if (iter->second.size() == 0)
                {
                    m_registeredInstances.erase(iter);
                }
            }
        }

        return *this;
    }

    /// Creates an instance using a registered factory
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T, class... TArgs>
    std::unique_ptr<T> createWithoutStoring [[nodiscard]] (TArgs... args)
    {
        return createByNameWithoutStoring<T>("", std::forward<TArgs>(args)...);
    }

    /// Creates an instance using a registered factory and assign it the specified name
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T, class... TArgs>
    std::unique_ptr<T> createByNameWithoutStoring
        [[nodiscard]] (const std::string& name, TArgs... args)
    {
        const char* typeName = getType<T>();

        if (m_registeredFactories.count(typeName))
        {
            // See if there is a factory that can create this object
            const auto& holder = m_registeredFactories.at(typeName);
            Factory<T, TArgs...> factory = boost::any_cast<Factory<T, TArgs...>>(holder);
            return std::unique_ptr<T>(factory(args...));
        }

        if (m_parent == nullptr)
        {
            using boost::format;
            using boost::str;
            static const format fmt("No registered factory exists which can create "
                                    "this object. "
                                    "Expected Holder Type = %1%, Name = %2%");
            BOOST_THROW_EXCEPTION(IocException()
                                  << StringInfo(str(format(fmt) % getType<T>() % name)));
        }

        return m_parent->createByNameWithoutStoring<T>(name, args...);
    }

    /// Creates an instance using a registered factory
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T, class... TArgs>
    std::shared_ptr<T> createWithoutStoringShared [[nodiscard]] (TArgs... args)
    {
        return createByNameWithoutStoringShared<T>("", std::forward<TArgs>(args)...);
    }

    /// Creates an instance using a registered factory and assign it the specified name
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T, class... TArgs>
    std::shared_ptr<T> createByNameWithoutStoringShared
        [[nodiscard]] (const std::string& name, TArgs... args)
    {
        const char* typeName = getType<T>();

        if (m_registeredFactories.count(typeName))
        {
            // See if there is a factory that can create this object
            const auto& holder = m_registeredFactories.at(typeName);
            SharedFactory<T, TArgs...> factory =
                boost::any_cast<SharedFactory<T, TArgs...>>(holder);
            return std::shared_ptr<T>(factory(args...));
        }

        if (m_parent == nullptr)
        {
            using boost::format;
            using boost::str;
            static const format fmt("No registered factory exists which can create "
                                    "this object. "
                                    "Expected Holder Type = %1%, Name = %2%");
            BOOST_THROW_EXCEPTION(IocException()
                                  << StringInfo(str(format(fmt) % getType<T>() % name)));
        }

        return m_parent->createByNameWithoutStoring<T>(name, args...);
    }

    /// Creates an instance using a registered factory
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T, class... TArgs>
    IocContainer& create(TArgs... args)
    {
        return createByName<T>("", args...);
    }

    /// Creates an instance using a registered factory and assign it the specified name
    /// @tparam T The type of the instance
    /// @returns Reference to the IocContainer, for chaining operations
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T, class... TArgs>
    IocContainer& createByName(const std::string& name, TArgs... args)
    {
        const char* typeName = getType<T>();

        if (m_registeredFactories.count(typeName))
        {
            const auto& holder = m_registeredFactories.at(typeName);
            const char* holderType = holder.type().name();
            const char* expectedHolderType = getType<SharedFactory<T, TArgs...>>();

            if (holderType == expectedHolderType)
            {
                auto factory = boost::any_cast<SharedFactory<T, TArgs...>>(holder);
                std::shared_ptr<T> inst(factory(args...));
                // See if there is a factory that can create this object
                bindInstance(name, std::move(inst));
            }
            else
            {
                auto factory = boost::any_cast<Factory<T, TArgs...>>(holder);
                std::unique_ptr<T> inst(factory(args...));
                // See if there is a factory that can create this object
                bindInstance(name, std::move(inst));
            }
        }
        else
        {
            if (m_parent == nullptr)
            {
                using boost::format;
                using boost::str;
                static const format fmt("No registered factory exists which can create "
                                        "this object. "
                                        "Expected Holder Type = %1%, Name = %2%");
                BOOST_THROW_EXCEPTION(
                    IocException() << StringInfo(str(format(fmt) % getType<T>() % name)));
            }

            m_parent->createByName<T>(name, args...);
        }

        return *this;
    }

    /// Checks whether the container holds an instance of that particular type
    /// @tparam T The type of the instance
    /// @returns \c true if contains an instance of that type; \c false otherwise
    template <class T>
    bool contains [[nodiscard]] () const
    {
        return contains<T>("");
    }

    /// Checks whether the container holds an instance or factory of that particular type
    /// and particular name
    /// @tparam T The type of the instance
    /// @param[in] name The name of the instance
    /// @returns \c true if contains an instance of that type; \c false otherwise
    template <class T>
    bool contains [[nodiscard]] (const std::string& name) const
    {
        Lock lock(m_mutex);

        const std::string typeName = getType<T>();

        auto iter = m_registeredInstances.find(typeName);

        if (iter != m_registeredInstances.end())
        {
            const auto& innerInstanceMap = iter->second;

            const auto& innerIter = innerInstanceMap.find(name);

            if (innerIter != innerInstanceMap.end())
            {
                return true;
            }
        }

        return m_registeredFactories.count(typeName) > 0;
    }

    /// Returns a copy of the object from within the IOC container. This should only be
    /// used
    ///     if the object is copy-constructible
    /// @tparam T The type of the instance
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    T get [[nodiscard]] () const
    {
        return get<T>("");
    }

    /// Returns a pointer to the object from within the IOC container. You should NOT
    /// explicitly
    //      delete the pointer if the object was created with any ownership semantics
    /// @tparam T The type of the instance
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    T* getPtr [[nodiscard]] () const
    {
        return getPtr<T>("");
    }

    /// Returns a reference to the object from within the IOC container. This is ideal if
    /// you inserted
    ///     an instance via a ref/cref semantic or a unique_ptr semantic
    /// @tparam T The type of the instance
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    T& getRef [[nodiscard]] () const
    {
        return getRef<T>("");
    }

    /// Returns a shared_ptr to the object from within the IOC container. This is ideal if
    /// you inserted
    ///     the object via the shared_ptr<> mechanism, because you intend to share
    ///     ownership
    /// @tparam T The type of the instance
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    std::shared_ptr<T> getShared [[nodiscard]] () const
    {
        return getShared<T>("");
    }

    /// Returns a copy of the object from within the IOC container. This should only be
    /// used
    ///     if the object is copy-constructible
    /// @tparam T The type of the instance
    /// @param name The name of the instance to retrieve
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    T get [[nodiscard]] (const std::string& name) const
    {
        using boost::format;
        using boost::str;

        Lock lock(m_mutex);

        const auto item = find<T>(name);

        const char* expectedHolderType = getType<HolderPtr<T>>();

        if (!item.first)
        {
            static const format fmt("Item not found by type and name. Expected Holder "
                                    "Type = %1%, Name = %2%");
            BOOST_THROW_EXCEPTION(IocException() << StringInfo(
                                      str(format(fmt) % expectedHolderType % name)));
        }

        auto innerIter = item.second;

        const Holder& holder = innerIter->second;
        const char* holderType = holder.type().name();

        if (holderType == expectedHolderType)
        {
            return *boost::any_cast<HolderPtr<T>>(holder);
        }

        static const format fmt("Holder type doesn't match expected holder type %1% != "
                                "%2%");
        BOOST_THROW_EXCEPTION(IocException() << StringInfo(
                                  str(format(fmt) % holderType % expectedHolderType)));
    }

    /// Returns a pointer to the object from within the IOC container. You should NOT
    /// explicitly
    //      delete the pointer if the object was created with any ownership semantics
    /// @tparam T The type of the instance
    /// @param name The name of the instance to retrieve
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    T* getPtr [[nodiscard]] (const std::string& name) const
    {
        return boost::any_cast<HolderPtr<T>>(getInternal<T>(name)).get();
    }

    /// Returns a reference to the object from within the IOC container. This is ideal if
    /// you inserted
    ///     an instance via a ref/cref semantic or a unique_ptr semantic
    /// @tparam T The type of the instance
    /// @param name The name of the instance to retrieve
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    T& getRef [[nodiscard]] (const std::string& name) const
    {
        return *boost::any_cast<HolderPtr<T>>(getInternal<T>(name)).get();
    }

    /// Returns a shared_ptr to the object from within the IOC container. This is ideal if
    /// you inserted
    ///     the object via the shared_ptr<> mechanism, because you intend to share
    ///     ownership
    /// @tparam T The type of the instance
    /// @param name The name of the instance to retrieve
    /// @returns The instance of the object from within the IOC container
    /// @throws IocException If object is not contained within the container and there is
    /// no factory
    ///     registered to create it
    template <class T>
    std::shared_ptr<T> getShared [[nodiscard]] (const std::string& name) const
    {
        return boost::any_cast<HolderPtr<T>>(getInternal<T>(name));
    }

    // Retrieve a static constant instance of this object for cases where we are calling
    // through to an IOC container, but have nothing to put in it. This will ensure the
    // correct object lifetime
    static const IocContainer& emptyContainer()
    {
        static const IocContainer empty;

        return empty;
    }

private:
    using RegisteredFactories = std::unordered_map<std::string, boost::any>;

    using Holder = boost::any;

    template <class T>
    using HolderPtr = std::shared_ptr<T>;

    using InnerRegisteredInstanceMap = std::unordered_map<std::string, Holder>;
    using RegisteredInstances =
        std::unordered_map<std::string, InnerRegisteredInstanceMap>;

    using Mutex = std::recursive_mutex;
    using Lock = std::unique_lock<Mutex>;

    // Helper to get types in a consistent way
    template <class T>
    const char* getType [[nodiscard]] () const
    {
        return typeid(std::decay_t<T>).name();
    }

    // Helper to get types in a consistent way
    template <class T>
    const char* getType [[nodiscard]] (const T&) const
    {
        return getType<T>();
    }

    // Internal helper method for finding the registered instance
    template <class T>
    std::pair<bool, InnerRegisteredInstanceMap::iterator> find
        [[nodiscard]] (const std::string& name, bool checkFactory = true)
    {
        Lock lock(m_mutex);

        // Internally does a const_cast to maximize code reuse
        auto res = const_cast<const IocContainer*>(this)->find<T>(name, checkFactory);

        return std::make_pair(
            res.first, const_cast<InnerRegisteredInstanceMap::iterator>(res.second));
    }

    // Internal helper method for finding the registered instance
    template <class T>
    std::pair<bool, InnerRegisteredInstanceMap::const_iterator> find
        [[nodiscard]] (const std::string& name, bool checkFactory = true) const
    {
        Lock lock(m_mutex);

        auto result = std::make_pair(false, InnerRegisteredInstanceMap::const_iterator());

        const std::string typeName = getType<T>();

        auto iter = m_registeredInstances.find(typeName);

        if (iter != m_registeredInstances.end())
        {
            const auto& innerInstanceMap = iter->second;

            const auto& innerIter = innerInstanceMap.find(name);

            if (innerIter != innerInstanceMap.end())
            {
                return std::make_pair(true, innerIter);
            }
        }

        if (checkFactory && m_registeredFactories.count(typeName))
        {
            // Attempt to create the object - Should throw if this also fails
            const_cast<IocContainer*>(this)->createByName<T>(name);

            // Avoid infinite recursion, in case it's still not found
            return find<T>(name, false);
        }

        return result;
    }

    // Internal helper method for the get method
    template <class T>
    Holder getInternal [[nodiscard]] (const std::string& name) const
    {
        using boost::format;
        using boost::str;

        Lock lock(m_mutex);

        const auto item = find<T>(name);

        const char* expectedHolderType = getType<HolderPtr<T>>();

        if (!item.first)
        {
            static const format fmt("Item not found by type and name. Expected Holder "
                                    "Type = %1%, Name = %2%");
            BOOST_THROW_EXCEPTION(IocException() << StringInfo(
                                      str(format(fmt) % expectedHolderType % name)));
        }

        auto innerIter = item.second;

        const Holder& holder = innerIter->second;

        const char* holderType = holder.type().name();

        if (holderType == expectedHolderType)
        {
            return innerIter->second;
        }

        static const format fmt("Holder type doesn't match expected holder type %1% != "
                                "%2%");
        BOOST_THROW_EXCEPTION(IocException() << StringInfo(
                                  str(format(fmt) % holderType % expectedHolderType)));
    }

    // Pointer to the parent container
    IocContainer* m_parent;

    // Container of registered factories
    RegisteredFactories m_registeredFactories;

    // Container of registered instances
    RegisteredInstances m_registeredInstances;

    // Keeps the container thread-safe
    mutable Mutex m_mutex;
};

//----------------------------------------------------------------------------------------------------------------------
} // cppinvert
//----------------------------------------------------------------------------------------------------------------------
