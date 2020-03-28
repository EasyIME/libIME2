#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <unknwn.h>
#include "ComPtr.h"
#include "ComObject.h"

using ::testing::Return;

class IUnknownMock : public Ime::ComObject<Ime::ComInterface<IUnknown>> {
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

TEST(TestComPtr, MakesNewPtr)
{
    auto ptr = Ime::ComPtr<IUnknownMock>::make();
    EXPECT_EQ(ptr->refCount(), 1);
}
TEST(TestComPtr, TakesOverIUnknownPtr)
{
    auto obj = new IUnknownMock();
    auto rawPtr = obj;
    auto ptr = Ime::ComPtr<IUnknownMock>::takeover(std::move(rawPtr));
    EXPECT_EQ(ptr, obj);
    EXPECT_EQ(obj->refCount(), 1);
    EXPECT_EQ(rawPtr, nullptr);
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

TEST(TestComPtr, Assignment)
{
    IUnknownMock obj, obj2;
    Ime::ComPtr<IUnknownMock> ptr;
    // Assign raw ptr.
    ptr = &obj;
    EXPECT_EQ(obj.refCount(), 2);

    // Assign another raw ptr.
    ptr = &obj2;
    EXPECT_EQ(obj.refCount(), 1);  // old ref is released.
    EXPECT_EQ(obj2.refCount(), 2);

    // Assign self
    ptr = ptr;
    EXPECT_EQ(obj2.refCount(), 2);  // ref count should not change.

    Ime::ComPtr<IUnknownMock> ptr2{ &obj };
    EXPECT_EQ(obj.refCount(), 2);
    // Assign another ComPtr
    ptr2 = ptr;  // both points to obj2 now.
    EXPECT_EQ(obj.refCount(), 1);  // old ref is released.
    EXPECT_EQ(obj2.refCount(), 3);

    // Assign nullptr
    ptr2 = nullptr;
    EXPECT_EQ(obj2.refCount(), 2);
    EXPECT_EQ(ptr2, nullptr);

    Ime::ComPtr<IUnknownMock> ptr3;
    // Move assignment
    ptr3 = std::move(ptr);
    EXPECT_EQ(ptr3, &obj2);
    EXPECT_EQ(obj2.refCount(), 2);  // ref count does not change.
    EXPECT_EQ(ptr, nullptr);  // moved ptr is cleared.
}
