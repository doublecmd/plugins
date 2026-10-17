#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <QApplication>
#include <QWidget>

typedef void* HWND;
typedef HWND (*ListLoadFunc)(HWND, char*, int);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_open <path_to_cert>" << std::endl;
        return 1;
    }
    int fakeArgc = 1;
    char pName[] = "test_open";
    char* fakeArgv[] = { pName, nullptr };
    QApplication app(fakeArgc, fakeArgv);

    void* handle = dlopen("/home/pplupo/cachyos_setup/dotfiles/.config/doublecmd/plugins/wlx/certview_qt6.wlx", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "dlopen failed: " << dlerror() << std::endl;
        return 1;
    }

    auto listLoad = (ListLoadFunc)dlsym(handle, "ListLoad");
    if (!listLoad) {
        std::cerr << "dlsym ListLoad failed" << std::endl;
        return 1;
    }

    QWidget parent;
    HWND res = listLoad(reinterpret_cast<HWND>(&parent), argv[1], 0);
    if (!res) {
        std::cerr << "ListLoad returned NULL for: " << argv[1] << std::endl;
        return 1;
    }

    std::cout << "ListLoad succeeded for: " << argv[1] << std::endl;
    return 0;
}
