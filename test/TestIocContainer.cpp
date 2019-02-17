#include <cppinvert/IocContainer.hpp>

#include <functional>
#include <iostream>
#include <random>
#include <thread>
#include <unordered_set>

#include <boost/format.hpp>

#include <boost/test/unit_test.hpp>

using namespace cppinvert;
using namespace std;

using boost::format;
using boost::str;

static constexpr const bool printObjectTracker = false;

// This structure can be used to help track when the objects are created or destroyed,
// so we can prove that the iocContainer is behaving correctly
class ObjectTracker
{
public:
    void onCreated(void* ptr)
    {
        if (printObjectTracker)
        {
            std::cout << "Creating " << ptr << std::endl;
        }

        BOOST_CHECK_MESSAGE(!activeObjects.count(ptr),
                            str(format("Expected to find no instances of ptr %1% "
                                       "in active objects at this point") %
                                ptr));
        activeObjects.insert(ptr);
    }

    void onDestroyed(void* ptr)
    {
        if (printObjectTracker)
        {
            std::cout << "Destroying " << ptr << std::endl;
        }

        BOOST_CHECK_MESSAGE(activeObjects.count(ptr) == 1,
                            str(format("Expected to find exactly one instance of ptr "
                                       "%1% in active objects at this point") %
                                ptr));
        activeObjects.erase(ptr);
    }

    void* firstObj()
    {
        return *(activeObjects.begin());
    }

    size_t size() const
    {
        return activeObjects.size();
    }

private:
    unordered_set<void*> activeObjects;
};

/// This structure wraps values, while also notifying the object tracker
template <class T>
struct ValWrapper
{
    ValWrapper(T newVal, ObjectTracker& tracker)
        : val(move(newVal))
        , objTracker(tracker)
    {
        objTracker.onCreated(this);
    }

    ValWrapper(const ValWrapper<T>& rhs)
        : val(rhs.val)
        , objTracker(rhs.objTracker)
    {
        objTracker.onCreated(this);
    }

    ValWrapper(ValWrapper<T>&& rhs)
        : val(move(rhs.val))
        , objTracker(rhs.objTracker)
    {
        objTracker.onCreated(this);
    }

    ~ValWrapper()
    {
        objTracker.onDestroyed(this);
    }

    T val;
    ObjectTracker& objTracker;
};

using IntWrapper = ValWrapper<int>;

class Fixture
{
public:
    ObjectTracker objTracker;
    IocContainer iocContainer;
};

BOOST_FIXTURE_TEST_SUITE(TestIocContainer, Fixture)

BOOST_AUTO_TEST_CASE(checkConstruction)
{
    IocContainer iocContainer;

    BOOST_CHECK_EQUAL(iocContainer.size(), 0);
}

BOOST_AUTO_TEST_CASE(checkUnnamedConstruction)
{
    auto v1 = 3.f;
    auto v2 = 9.9;
    const char* v3 = "HELLO";
    string v4 = "GOODBYE";

    iocContainer.bindInstance(val(v1)).bindInstance(val(v2)).bindInstance(v3).bindInstance(
        val(v4));

    BOOST_CHECK_EQUAL(iocContainer.size(), 4);
    BOOST_CHECK(iocContainer.contains<float>());
    BOOST_CHECK(iocContainer.contains<double>());
    BOOST_CHECK(iocContainer.contains<const char>());
    BOOST_CHECK(iocContainer.contains<string>());

    BOOST_CHECK_EQUAL(v1, iocContainer.get<float>());
    BOOST_CHECK_EQUAL(v2, iocContainer.get<double>());
    BOOST_CHECK_EQUAL(v3, iocContainer.getPtr<const char>());
    BOOST_CHECK_EQUAL(v4, iocContainer.get<string>());
}

BOOST_AUTO_TEST_CASE(checkSubContainerConstruction)
{
    IocContainer subContainer1;
    IocContainer subContainer2;
    IocContainer subContainer3;

    iocContainer.bindInstance("sub1", ref(subContainer1))
        .bindInstance("sub2", ref(subContainer2))
        .bindInstance("sub3", ref(subContainer3))
        .bindInstance("int", val(3));

    BOOST_CHECK_EQUAL(iocContainer.size(), 4);
    BOOST_CHECK(iocContainer.contains<IocContainer>("sub1"));
    BOOST_CHECK(iocContainer.contains<IocContainer>("sub2"));
    BOOST_CHECK(iocContainer.contains<IocContainer>("sub3"));
    BOOST_CHECK(iocContainer.contains<int>("int"));

    BOOST_CHECK_EQUAL(&subContainer1, &iocContainer.getRef<IocContainer>("sub1"));
    BOOST_CHECK_EQUAL(&subContainer2, &iocContainer.getRef<IocContainer>("sub2"));
    BOOST_CHECK_EQUAL(&subContainer3, &iocContainer.getRef<IocContainer>("sub3"));
    BOOST_CHECK_EQUAL(3, iocContainer.get<int>("int"));
}

BOOST_AUTO_TEST_CASE(testIocContainerInThread)
{
    static const string testFactory("com.cppinvert.testfactory");

    static const string ip = "127.0.0.1";
    static const size_t port = 9999;

    IocContainer subIocContainer;
    iocContainer.bindInstance(testFactory, ref(subIocContainer));
    subIocContainer.bindInstance("ip", val(ip)).bindInstance("port", val(port));

    auto testInThread = [this, &subIocContainer]() {
        BOOST_CHECK(iocContainer.contains<IocContainer>(testFactory));

        auto& subContainer = iocContainer.getRef<IocContainer>(testFactory);
        BOOST_CHECK(subContainer.contains<string>("ip"));
        BOOST_CHECK(subContainer.contains<size_t>("port"));
        BOOST_CHECK_EQUAL(subContainer.get<string>("ip"), ip);
        BOOST_CHECK_EQUAL(subContainer.get<size_t>("port"), port);
    };

    thread t(testInThread);
    t.join();
}

BOOST_AUTO_TEST_CASE(testIocContainerFactory)
{
    std::string str;

    iocContainer.getRef<IocContainer>("sub1")
        .bindInstance<int>("3", val(3))
        .bindInstance<char>("a", val('a'))
        .bindInstance<char>("b", val('b'));
    iocContainer.getRef<IocContainer>("sub2")
        .bindInstance<int>("4", val(4))
        .bindInstance<char>("z", val('z'))
        .bindValue<int>("5", 5)
        .bindInstance<std::string>(ref(str));

    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
    BOOST_CHECK_EQUAL(iocContainer.size(true), 9);
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub1").size(), 3);
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub2").size(), 4);
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub1").get<int>("3"), 3);
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub1").get<char>("a"), 'a');
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub1").get<char>("b"), 'b');
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub2").get<int>("4"), 4);
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub2").get<char>("z"), 'z');
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub2").get<int>("5"), 5);
    BOOST_CHECK_EQUAL(*iocContainer.getRef<IocContainer>("sub2").getPtr<int>("5"), 5);
    BOOST_CHECK_EQUAL(iocContainer.getRef<IocContainer>("sub2").getRef<int>("5"), 5);
    BOOST_CHECK_EQUAL(*iocContainer.getRef<IocContainer>("sub2").getShared<int>("5"), 5);
    BOOST_CHECK_EQUAL(&iocContainer.getRef<IocContainer>("sub2").getRef<std::string>(),
                      &str);
}

BOOST_AUTO_TEST_CASE(testIocContainerFactoryMultiples)
{
    class ISomething
    {
    public:
        virtual ~ISomething()
        {
        }
    };

    class SomethingElse : virtual public ISomething
    {
    };

    IocContainer::Factory<ISomething> factory = []() {
        return std::unique_ptr<ISomething>(new SomethingElse());
    };
    iocContainer.registerFactory<ISomething>(factory);

    BOOST_CHECK_EQUAL(iocContainer.size(), 0);
    ISomething* a1 = iocContainer.getPtr<ISomething>("a");
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    ISomething* a2 = iocContainer.getPtr<ISomething>("a");
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    BOOST_CHECK_EQUAL(a1, a2);
    std::shared_ptr<ISomething> a3 = iocContainer.getShared<ISomething>("a");
    BOOST_CHECK_EQUAL(a2, a3.get());
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    ISomething& b1 = iocContainer.getRef<ISomething>("b");
    BOOST_CHECK(a1 != &b1);
    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
    ISomething& b2 = iocContainer.getRef<ISomething>("b");
    BOOST_CHECK_EQUAL(&b1, &b2);
    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
    std::shared_ptr<ISomething> b3 = iocContainer.getShared<ISomething>("b");
    BOOST_CHECK_EQUAL(&b2, b3.get());
    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
}

BOOST_AUTO_TEST_CASE(testIocContainerSharedFactoryMultiples)
{
    class ISomething
    {
    public:
        virtual ~ISomething()
        {
        }
    };

    class SomethingElse : virtual public ISomething
    {
    };

    IocContainer::SharedFactory<ISomething> factory = []() {
        return std::shared_ptr<ISomething>(new SomethingElse());
    };
    iocContainer.registerFactory<ISomething>(factory);

    BOOST_CHECK_EQUAL(iocContainer.size(), 0);
    ISomething* a1 = iocContainer.getPtr<ISomething>("a");
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    ISomething* a2 = iocContainer.getPtr<ISomething>("a");
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    BOOST_CHECK_EQUAL(a1, a2);
    std::shared_ptr<ISomething> a3 = iocContainer.getShared<ISomething>("a");
    BOOST_CHECK_EQUAL(a2, a3.get());
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    ISomething& b1 = iocContainer.getRef<ISomething>("b");
    BOOST_CHECK(a1 != &b1);
    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
    ISomething& b2 = iocContainer.getRef<ISomething>("b");
    BOOST_CHECK_EQUAL(&b1, &b2);
    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
    std::shared_ptr<ISomething> b3 = iocContainer.getShared<ISomething>("b");
    BOOST_CHECK_EQUAL(&b2, b3.get());
    BOOST_CHECK_EQUAL(iocContainer.size(), 2);
}

BOOST_AUTO_TEST_CASE(testIocContainerFactoryTemplateParameterPack)
{
    class IObject
    {
    public:
        virtual ~IObject()
        {
        }
    };

    class Point : virtual public IObject
    {
    public:
        Point(int x, int y)
            : m_x(x)
            , m_y(y)
        {
        }

        int m_x;
        int m_y;
    };

    IocContainer::Factory<IObject, int, int> factory = [](int x, int y) {
        return std::unique_ptr<IObject>(new Point(x, y));
    };
    iocContainer.registerFactory<IObject>(factory);

    BOOST_CHECK_EQUAL(iocContainer.size(), 0);
    IObject& a = iocContainer.create<IObject>(3, 4).getRef<IObject>();
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    Point& p = dynamic_cast<Point&>(a);
    BOOST_CHECK_EQUAL(p.m_x, 3);
    BOOST_CHECK_EQUAL(p.m_y, 4);
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
}

BOOST_AUTO_TEST_CASE(testIocContainerSharedFactoryTemplateParameterPack)
{
    class IObject
    {
    public:
        virtual ~IObject()
        {
        }
    };

    class Point : virtual public IObject
    {
    public:
        Point(int x, int y)
            : m_x(x)
            , m_y(y)
        {
        }

        int m_x;
        int m_y;
    };

    IocContainer::SharedFactory<IObject, int, int> factory = [](int x, int y) {
        return std::unique_ptr<IObject>(new Point(x, y));
    };
    iocContainer.registerFactory<IObject>(factory);

    BOOST_CHECK_EQUAL(iocContainer.size(), 0);
    IObject& a = iocContainer.create<IObject>(3, 4).getRef<IObject>();
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
    Point& p = dynamic_cast<Point&>(a);
    BOOST_CHECK_EQUAL(p.m_x, 3);
    BOOST_CHECK_EQUAL(p.m_y, 4);
    BOOST_CHECK_EQUAL(iocContainer.size(), 1);
}

BOOST_AUTO_TEST_CASE(testSubContainerRetrieval)
{
    static const char* uuid("TcpConnection");
    static const std::string uuid2(uuid);
    static const std::string ip("127.0.0.1");
    static const std::size_t port(9999);

    iocContainer.getRef<IocContainer>(uuid)
        .bindValue<std::string>("ip", ip)
        .bindValue<std::size_t>("port", port);

    IocContainer& subIocContainer = iocContainer.getRef<IocContainer>(uuid2);

    std::string retrievedIp = subIocContainer.get<std::string>("ip");
    std::size_t retrievedPort = subIocContainer.get<std::size_t>("port");

    BOOST_CHECK_EQUAL(retrievedIp, ip);
    BOOST_CHECK_EQUAL(retrievedPort, port);
}

BOOST_AUTO_TEST_CASE(testBindInstanceAndThenEraseInstance)
{
    auto v1 = 3.f;
    auto v2 = 9.9;
    const char* v3 = "HELLO";
    string v4 = "GOODBYE";

    iocContainer.bindValue(v1).bindValue(v2).bindInstance(v3).bindValue(v4);

    BOOST_CHECK_EQUAL(iocContainer.size(), 4);
    BOOST_CHECK(iocContainer.contains<float>());
    BOOST_CHECK(iocContainer.contains<double>());
    BOOST_CHECK(iocContainer.contains<const char>());
    BOOST_CHECK(iocContainer.contains<string>());

    iocContainer.eraseInstance<const char>();

    BOOST_CHECK_EQUAL(v1, iocContainer.get<float>());
    BOOST_CHECK_EQUAL(v2, iocContainer.get<double>());
    BOOST_CHECK(!iocContainer.contains<const char>());
    BOOST_CHECK_EQUAL(v4, iocContainer.get<string>());
}

BOOST_AUTO_TEST_CASE(testPolymorphism)
{
    struct A
    {
        A()
        {
        }

        explicit A(int p_x)
            : x(p_x)
        {
        }

        int x{0};
    };

    struct B : public A
    {
        B()
        {
        }

        explicit B(int p_x, int p_y)
            : A(p_x)
            , y(p_y)
        {
        }

        int y{0};
    };

    struct C : public A
    {
        C()
        {
        }

        explicit C(int p_x, int p_z)
            : A(p_x)
            , z(p_z)
        {
        }

        int z{0};
    };

    A a{1};
    B b{2, 3};
    C c{4, 6};

    iocContainer.bindInstance("a_a", ref(a))
        .bindInstance("b_b", ref(b))
        .bindInstance<A>("b_a", ref(b))
        .bindInstance("c_c", ref(c))
        .bindInstance<A>("c_a", ref(c));

    BOOST_CHECK_EQUAL(iocContainer.size(), 5);
    BOOST_CHECK(iocContainer.contains<A>("a_a"));
    BOOST_CHECK(iocContainer.contains<B>("b_b"));
    BOOST_CHECK(iocContainer.contains<A>("b_a"));
    BOOST_CHECK(!iocContainer.contains<B>("b_a"));
    BOOST_CHECK(iocContainer.contains<C>("c_c"));
    BOOST_CHECK(iocContainer.contains<A>("c_a"));
    BOOST_CHECK(!iocContainer.contains<C>("c_a"));

    BOOST_CHECK_EQUAL(&a, &iocContainer.getRef<A>("a_a"));
    BOOST_CHECK_EQUAL(&b, &iocContainer.getRef<B>("b_b"));
    BOOST_CHECK_EQUAL(&b, &iocContainer.getRef<A>("b_a"));
    BOOST_CHECK_EQUAL(&c, &iocContainer.getRef<C>("c_c"));
    BOOST_CHECK_EQUAL(&c, &iocContainer.getRef<A>("c_a"));
}

BOOST_AUTO_TEST_CASE(testMoveInstance)
{
    // Put in scope, so moved instance of a1 is removed from tracker before checking
    {
        IntWrapper a1{3, objTracker};

        BOOST_CHECK_EQUAL(objTracker.size(), 1);

        // Do a bind instance where the container takes ownership of the object
        iocContainer.bindInstance(mval(a1));
    }

    BOOST_CHECK_EQUAL(objTracker.size(), 1);
}

BOOST_AUTO_TEST_CASE(testReBindInstance)
{
    // Put in scope, so moved instance of a1 is removed from tracker before checking
    {
        IntWrapper a1{3, objTracker};

        BOOST_CHECK_EQUAL(objTracker.size(), 1);

        // Do a bind instance where the container takes ownership of the object
        iocContainer.bindInstance(mval(a1));
    }

    BOOST_CHECK_EQUAL(objTracker.size(), 1);

    void* firstObj = objTracker.firstObj();

    // Put in scope, so moved instance of a2 is removed from tracker before checking
    {
        IntWrapper a2{4, objTracker};

        iocContainer.bindInstance(mval(a2));
    }

    BOOST_CHECK_EQUAL(objTracker.size(), 1);
    BOOST_CHECK_NE(objTracker.firstObj(), firstObj);
}

//----------------------------------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_SUITE_END()
//----------------------------------------------------------------------------------------------------------------------
