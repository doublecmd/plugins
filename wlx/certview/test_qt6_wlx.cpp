#include <QApplication>
#include <QWidget>
#include <dlfcn.h>
#include <iostream>
#include <cassert>

typedef void* HWND;
typedef HWND (*ListLoadFunc)(HWND ParentWin, char* FileToLoad, int ShowFlags);
typedef void (*ListCloseWindowFunc)(HWND ListWin);
typedef void (*ListGetDetectStringFunc)(char* DetectString, int maxlen);

int main(int argc, char* argv[]) {
    int fakeArgc = 1;
    char fakeProgName[] = "test_qt6_wlx";
    char* fakeArgv[] = { fakeProgName, nullptr };
    QApplication app(fakeArgc, fakeArgv);

    void* handle = dlopen("./certview_qt6.wlx", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "Failed to dlopen certview_qt6.wlx: " << dlerror() << std::endl;
        return 1;
    }

    auto listLoad = (ListLoadFunc)dlsym(handle, "ListLoad");
    auto listCloseWindow = (ListCloseWindowFunc)dlsym(handle, "ListCloseWindow");
    auto listGetDetectString = (ListGetDetectStringFunc)dlsym(handle, "ListGetDetectString");

    assert(listLoad != nullptr);
    assert(listCloseWindow != nullptr);
    assert(listGetDetectString != nullptr);

    char detect[256] = {0};
    listGetDetectString(detect, sizeof(detect));
    std::cout << "Detect string: " << detect << std::endl;

    QWidget parent;
    char cerPath[] = "../testdata/test_rsa.cer";
    HWND win = listLoad(reinterpret_cast<HWND>(&parent), cerPath, 0);
    assert(win != nullptr);
    std::cout << "Successfully loaded test_rsa.cer into Qt6 widget" << std::endl;
    listCloseWindow(win);

    char pemPath[] = "../testdata/test_rsa.pem";
    win = listLoad(reinterpret_cast<HWND>(&parent), pemPath, 0);
    assert(win != nullptr);
    std::cout << "Successfully loaded test_rsa.pem into Qt6 widget" << std::endl;
    listCloseWindow(win);

    char p12Path[] = "../testdata/test_protected.p12";
    win = listLoad(reinterpret_cast<HWND>(&parent), p12Path, 0);
    assert(win != nullptr);
    std::cout << "Successfully loaded test_protected.p12 into Qt6 widget" << std::endl;
    listCloseWindow(win);

    dlclose(handle);
    std::cout << "All Qt6 WLX runtime tests passed!" << std::endl;
    return 0;
}
