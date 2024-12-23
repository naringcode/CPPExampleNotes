// Update Date : 2024-12-14
// OS : Windows 10 64bit
// Program : Visual Studio 2022
// Version : C++20
// Configuration : Debug-x64, Release-x64

#include <iostream>
#include <memory>

#include <type_traits>
#include <utility> // index_sequence

using namespace std;

// !! 자세한 내용은 shared_ptr_details.cpp에 적었다. !!
// !! 아래 코드는 간소화하여 사용자 정의 Deleter를 적용하는 방법을 기술하였다. !!

// 순서대로 볼 것
// 
// # shared_ptr을 사용할 경우 알아야 할 기본적인 내용
// 1. shared_ptr_with_deleter.cpp <--
// 2. shared_ptr_with_allocator.cpp
// 3. shared_ptr_with_deleter_and_allocator.cpp
// 4. (중요) shared_ptr_details.cpp (SFINAE 내용이 포함됨)
// 
// # weak_ptr의 유효성 검증 로직에 대한 내용
// 5. weak_ptr_details.cpp
//
// # shared_ptr의 관리 객체에서 자신을 반환할 때 필요한 내용
// 6. enable_shared_from_this.cpp
// 7. enable_shared_from_this_details.cpp
//
// # shared_ptr을 멀티스레딩 환경에서 사용할 때 발생할 수 있는 문제점을 기술한 내용
// 8. allocation_aligned_byte_boundaries.cpp(사전지식)
// 9. (중요) smart_pointer_multi_threading_issues.cpp

// 용어 정리
// 
// 관리 객체(Managed Object) : 스마트 포인터를 통해서 사용 및 관리하고자 하는 대상 객체(이것의 메모리를 "관리 객체의 메모리"라고 칭할 것임)
// 레퍼런스 카운팅 블록 : 참조 카운트를 관리하기 위한 블록
// 컨트롤 블록(다음 내용을 포함하는 개념)
// : 레퍼런스 카운팅 관리
// : 객체의 삭제 방식이나 메모리 관리 방식을 지정하기 위한 Deleter나 Allocator를 가질 수 있음.
// : 디버깅이나 메모리 관리 및 프로파일링을 위한 메타 데이터를 가질 수 있음.
// : 컨트롤 블록 안에 관리 객체의 메모리를 할당하여 최적화하는 경우도 있음(둘은 다른 개념).
//
// 컨트롤 블록은 레퍼런스 카운팅 블록을 포함하는 상위 개념이지만 찾아보면 두 개념을 동일하게 보는 경우가 많다.
// 컨트롤 블록 안에 관리 객체의 메모리를 함께 할당하는 경우도 있지만 이건 최적화를 위해서 그렇게 한 것일 뿐, 관리 객체와 컨트롤 블록 두 개념은 별개로 봐야 한다.
//
// Deleter(삭제자) : 관리 객체의 수명이 다했을 때 이를 해제할 방식이 기술되어 있는 Callable
// Allocator(할당자) : 컨트롤 블록을 할당하고 해제하기 위해 사용할 클래스
//
// Deleter는 관리 객체와 연관되어 있고, Allocator는 컨트롤 블록과 연관되어 있다.
// 할당자의 deallocate()와 Deleter는 개념상 서로 연관성이 없지만 인터페이스만 보고 기능을 유추하려 하면 혼란스러울 수 있다.
// !! 할당자의 deallocate()와 Deleter는 서로 연관성이 없으며 아예 독립된 별개의 기능을 수행한다. !!

/***************************
*      Object Classes      *
***************************/

class TestObject
{
public:
    TestObject(int valA) : _valA{ valA }
    {
        cout << "TestObject()\n";

        cout << std::hex << "\tPtr : 0x" << this << "\n\n";
        cout << std::dec;
    }

    /* virtual */ ~TestObject()
    {
        cout << "~TestObject()\n";

        cout << std::hex << "\tPtr : 0x" << this << "\n\n";
        cout << std::dec;
    }

private:
    int _valA = 0;
};

class TestObjectEx : public TestObject
{
public:
    TestObjectEx(int valA, double valB)
        : TestObject{ valA }, _valB{ valB }
    {
        cout << "TestObjectEx()\n";

        cout << std::hex << "\tPtr : 0x" << this << "\n\n";
        cout << std::dec;
    }

    /* virtual */ ~TestObjectEx()
    {
        cout << "~TestObjectEx()\n";

        cout << std::hex << "\tPtr : 0x" << this << "\n\n";
        cout << std::dec;
    }

private:
    double _valB = 0.0;
};

/********************
*      Printer      *
********************/

template <typename... Args, size_t... Is>
void PrintArgs(std::index_sequence<Is...>, Args&&... args)
{
    // 폴드 표현식으로 인자 출력
    ((cout << "\t\tArg[" << Is << "] Type : " << typeid(args).name() << ", Value : " << args << '\n'), ...);
    cout << '\n';
}

/********************
*      Deleter      *
********************/

template <typename Type>
void MyDeleter(Type* ptr)
{
    cout << "MyDeleter<Type>()\n" << "\tType: " << typeid(Type).name() << "\n";

    cout << std::hex << "\t\tPtr : 0x" << ptr << "\n\n";
    cout << std::dec;

    delete ptr;

    // 관리 객체의 메모리를 순수하게 날것 그대로 할당해서 관리하는 방식을 사용할 때는 소멸자를 직접 호출해야 한다.
    // !! 메모리 풀을 구현할 경우 다음 코드가 필요할 수 있음. !!
    // if constexpr (std::is_class_v<Type>)
    // {
    //     ptr->~Type();
    // }
    // 
    // ::free(ptr);
}

/****************************************
*      Custom Make Shared Function      *
****************************************/

template <typename Type, typename... Args>
shared_ptr<Type> MyMakeSharedWithDeleter(Args&&... args)
{
    cout << "MyMakeSharedWithDeleter()\n" << "\tType : " << typeid(Type).name() << '\n';

    // 폴드 표현식으로 인자 출력(인자의 번호까지는 출력할 수 없음(내가 방법을 모르는 것일 수도))
    // ((cout << "\tArg Type : " << typeid(args).name() << ", Value : " << args << '\n'), ...);

    // sizeof...()은 가변인자 템플릿의 인자 개수를 구하기 위해서 사용한 것임(인자 팩을 확장하는 것이 아니니까 혼동하지 말 것).
    // 완벽한 전달의 의미를 생각하면 인자는 복사 방식으로 전달하는 것이 좋음(std::forward()로 전달하면 new Type{ std::forward<Args>(args)... }에서 문제가 생길 수 있음).
    PrintArgs(std::make_index_sequence<sizeof...(args)>(), args...); // 전달을 우회할 순 있으나 너무 설명적인 코드가 되니 생략함(쉽게 쉽게 가자).

    // --------------------------------------------------

    // 이런저런 코드를 테스트해봤을 때 std::forward() 말고 std::forward<Args>()를 써야 하는 경우가 있었음(경험적인 부분).

    // 생성자에 인자를 넣을 때 중괄호 "{}" 안에 넣는 것이 내 스타일의 코드지만 템플릿의 가변 타입까지 범위를 확대하면 에러가 났을 때 찾기가 매우 힘들었음.
    // !! 생성자의 인자를 중괄호 안에 넣어서 처리하면 암시적 형변환을 허용하지 않음(중괄호 안에 넣긴 했으나 이건 리스트 초기화가 아님). !!
    // Type* ptr = new Type{ std::forward<Args>(args)... };

    // 템플릿의 가변 타입을 생성자의 인자로 사용할 때는 암시적인 형변환을 허용하는 방식을 쓰도록 함.
    // Type* ptr = new Type(std::forward<Args>(args)...);
    
    // 어떤 Deleter를 호출할 것인지 미리 지정하고 shared_ptr을 생성한다.
    // return shared_ptr<Type>{ ptr, &MyDeleter<Type> };

    // 관리 객체의 메모리를 순수하게 날것 그대로 할당해서 관리하는 방식을 사용할 때는 placement new로 생성자를 직접 호출해야 한다.
    Type* ptr = static_cast<Type*>(::malloc(sizeof(Type)));
    
    cout << "Allocating by new...\n" << "\tType : " << typeid(Type).name() << '\n';
    cout << std::hex << "\t\tPtr : 0x" << ptr << "\n\n";
    cout << std::dec;

    new (ptr) Type(std::forward<Args>(args)...); // placement new로 객체의 생성자 호출
    
    return shared_ptr<Type>{ ptr, &MyDeleter<Type> };

    // 다음과 같이 람다식을 구성해서 Deleter를 전달하는 것도 가능하다.
    // 
    // 다만 람다식은 컴파일 과정에서 Functor로 변환되고, 이를 생성하는 비용이 있기 때문에
    // 람다식을 통한 Deleter 전달은 단순 함수 포인터를 전달하는 방식보다 약간 오버헤드가 발생할 수 있다(그 차이는 매우 미미하긴 함).
    // 
    // !! 추가적인 오버헤드는 매우 미미하고, 요즘은 컴파일러가 알아서 최적화를 잘 해주기 때문에 큰 문제가 되는 부분은 아님. !!
    // return shared_ptr<Type>{ ptr, 
    //                         [](Type* ptrParam)
    //                         {
    //                             cout << "# Begin - Lambda Deleter\n\n";
    // 
    //                             delete ptrParam;
    // 
    //                             cout << "# End - Lambda Deleter\n\n";
    //                         } };
}

/*****************
*      Main      *
*****************/

int main()
{
    // !! Visual Studio 2022 기준이며 아래 분석 내용은 컴파일러 버전에 따라 달라질 수 있음. !!

    cout << "-------------------------#01#-------------------------\n\n";

    // Ptr, Deleter를 생성자의 인자로 넘겨 스마트 포인터를 생성하는 방식
    {
        // RVO에 의해 대상 메모리 공간에 직접적으로 Ptr, Deleter를 받는 생성자가 적용됨.
        shared_ptr<TestObjectEx> objEx = MyMakeSharedWithDeleter<TestObjectEx>(100, 3.141592);

        cout << "## End Of Block ##\n\n";
    }

    cout << "-------------------------#02#-------------------------\n\n";

    // 사용 도중에 nullptr을 대입했을 경우, nullptr을 가리키는 스마트 포인터가 Deleter를 호출하는지 확인하기 위한 코드
    {
        // RVO에 의해 대상 메모리 공간에 직접적으로 Ptr, Deleter를 받는 생성자가 적용됨.
        shared_ptr<TestObjectEx> objEx1 = MyMakeSharedWithDeleter<TestObjectEx>(100, 3.141592);

        {
            // 복사 생성자
            shared_ptr<TestObjectEx> objEx2 = objEx1;

            // shared_ptr(nullptr_t) 생성자로 임시 객체 생성 후, 이를 objEx1에 반영하기 위한 이동 대입 연산자가 호출됨.
            objEx1 = nullptr; // nullptr은 암시적으로 shared_ptr{ nullptr }로 변환됨.

            cout << "assign nullptr...\n\n";

            // 해당 블록을 빠져나오면 shared_ptr의 소멸자를 거치며 관리 객체의 소멸자를 호출함.
        }

        cout << "## End Of Block ##\n\n";

        // End Of Block 이후 Deleter가 호출되진 않음.
        // 사용 도중 objEx1에 nullptr을 대입해도 문제 없음.
    }

    cout << "-------------------------#03#-------------------------\n\n";

    // 업캐스팅하여 스마트 포인터를 사용할 경우 어떤 소멸자가 호출되는지 관찰하기 위한 코드
    {
        shared_ptr<TestObject> obj;

        {
            // RVO에 의해 대상 메모리 공간에 직접적으로 Ptr, Deleter를 받는 생성자가 적용됨.
            shared_ptr<TestObjectEx> objEx = MyMakeSharedWithDeleter<TestObjectEx>(300UL, 2.718281f);

            // 인자로 넘긴 objEx의 관리 객체의 주소를 캐스팅하여 shared_ptr<TestObject>를 생성하고
            // 이를 obj에 반영하기 위한 이동 대입 연산자가 호출됨.
            obj = static_pointer_cast<TestObject>(objEx);
        }

        cout << "after static_pointer_cast...\n\n";

        // !! 관리 객체의 소멸자 호출은 shared_ptr<TestObject>의 소멸자를 통해서 이루어짐(shared_ptr<TestObjectEx>를 통해서 넘겼다고 해도). !!
        // !! shared_ptr<TestObject>의 소멸자를 거친다고 해도 이게 ~TestObject()를 호출하겠다는 뜻은 아님. !!
        // !! shared_ptr<TestObject>가 전달받은 관리 객체는 TestObjectEx이며, 무엇보다 MyDeleter<TestObjectEx>를 묶어서 컨트롤 블록을 생성한 상태임. !!

        // 어찌되었든 shared_ptr<TestObject>의 소멸자를 거치긴 해도 관리 객체의 소멸 단계에서 호출되는 건 생성 당시 등록한 Deleter이다.
        // 등록한 Deleter를 타고 들어가면 실행되는 건 "delete ptr"인데 이 ptr의 타입은 TestObjectEx이다.
        // 삭제하는 ptr이 TestObjectEx 타입이기 때문에 최종적으로 호출되는 객체의 소멸자는 ~TestObjectEx()이다.

        // TestObject의 소멸자에 virtual을 붙이지 않아도 MyDeleter<TestObjectEx>()를 통해 직접적으로 TestObjectEx를 삭제한다.
        // static_pointer_cast<T>()를 통해 부모로 업캐스팅 했다고 해도 파생 클래스를 직접적으로 삭제하는 방식이기 때문에
        // 굳이 부모 클래스의 소멸자를 가상화하지 않아도 소멸자 호출 과정이 제대로 단계별로 이루어지는 것이다.
        // !! 애초에 가상 함수 테이블을 타고 들어가 소멸자를 호출하는 방식이 아니니까. !!

        // 자세한 건 shared_ptr_details.cpp에 적어두었다.

        cout << "## End Of Block ##\n\n";
    }

    cout << "--------------------------------------------------\n";

    return 0;
}
