#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <unknwn.h>
#include "ComPtr.h"
#include "ComObject.h"

using ::testing::Return;

class IUnknownMock : public Ime::ComObject<> {
public:
    MOCK_METHOD(HRESULT, QueryInterface, (REFIID riid, void** ppvObject), (Calltype(STDMETHODCALLTYPE), override));
};

TEST(TestComPtr, DefaultsToNull)
{
	Ime::ComPtr<IUnknownMock> ptr;
	EXPECT_EQ(ptr, nullptr);
}

TEST(TestComPtr, InitFromIUnknownPtr)
{
	IUnknownMock obj;
    Ime::ComPtr<IUnknownMock> ptr(&obj);
    EXPECT_EQ(ptr, &obj);
    EXPECT_EQ(obj.refCount(), 2);
}

TEST(TestComPtr, TakesOverIUnknownPtr)
{
    auto obj = new IUnknownMock();
    Ime::ComPtr<IUnknownMock> ptr(obj, false);
    EXPECT_EQ(ptr, obj);
    EXPECT_EQ(obj->refCount(), 1);
}

TEST(TestComPtr, Destructor)
{
    IUnknownMock obj;
    {
        Ime::ComPtr<IUnknownMock> ptr(&obj);
        EXPECT_EQ(obj.refCount(), 2);
    }
    EXPECT_EQ(obj.refCount(), 1);
}

TEST(TestComPtr, AddRefOnCopy)
{
    IUnknownMock obj;
    Ime::ComPtr<IUnknownMock> ptr{&obj};
    EXPECT_EQ(obj.refCount(), 2);

    Ime::ComPtr<IUnknownMock> ptr2{ ptr };
    EXPECT_EQ(obj.refCount(), 3);
    EXPECT_EQ(ptr2, &obj);
}

TEST(TestComPtr, MoveConstructor)
{
    IUnknownMock obj;
    Ime::ComPtr<IUnknownMock> ptr{ &obj };
    EXPECT_EQ(obj.refCount(), 2);

    Ime::ComPtr<IUnknownMock> ptr2{ std::move(ptr) };
    EXPECT_EQ(obj.refCount(), 2);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(ptr2, &obj);
}
