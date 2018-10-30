#define BOOST_TEST_MODULE Test

#include <cppinvert/IocContainer.hpp>

#include <functional>
#include <random>
#include <thread>

#include <boost/test/included/unit_test.hpp>
#include <boost/test/unit_test.hpp>

using namespace cppinvert;
using namespace std;

class Fixture
{
public:
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

    iocContainer.bindInstance(v1)
                .bindInstance(v2)
                .bindInstance(v3)
                .bindInstance(v4);

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
                .bindInstance("int", 3);

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
    subIocContainer.bindInstance("ip", ip).
        bindInstance("port", port);

     auto testInThread = [this, &subIocContainer]()
     {
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

    iocContainer.getRef<IocContainer>("sub1").
        bindInstance<int>("3", 3).
        bindInstance<char>("a", 'a').
        bindInstance<char>("b", 'b');
    iocContainer.getRef<IocContainer>("sub2").
        bindInstance<int>("4", 4).
        bindInstance<char>("z", 'z').
        bindInstance<int>("5", 5).
        bindInstance<std::string>(ref(str));

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
    BOOST_CHECK_EQUAL(&iocContainer.getRef<IocContainer>("sub2").getRef<std::string>(), &str);
}

BOOST_AUTO_TEST_CASE(testIocContainerFactoryMultiples)
{
    class ISomething
    {
    public:
        virtual ~ISomething() {}
    };

    class SomethingElse : virtual public ISomething
    {};

    IocContainer::Factory<ISomething> factory = []() { return std::unique_ptr<ISomething>(new SomethingElse()); };
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
        virtual ~IObject() {}
    };

    class Point : virtual public IObject
    {
    public:
        Point(int x, int y) :
            m_x(x), m_y(y)
        {

        }

        int m_x;
        int m_y;
    };

    IocContainer::Factory<IObject, int, int> factory =
        [](int x, int y) { return std::unique_ptr<IObject>(new Point(x, y)); };
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

    iocContainer.getRef<IocContainer>(uuid).
        bindInstance<std::string>("ip", ip).
        bindInstance<std::size_t>("port", port);

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

    iocContainer.bindInstance(v1)
                .bindInstance(v2)
                .bindInstance(v3)
                .bindInstance(v4);

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

//----------------------------------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_SUITE_END()
//----------------------------------------------------------------------------------------------------------------------
