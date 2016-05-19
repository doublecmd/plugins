#import "quicklook.h"
//#include <QWidget>
#include "QtGui/QWidget"

@implementation QLItem
@end

extern "C" {

void* ListLoad(void* parentWin, char* fileToLoad, int showFlags) {
    QWidget* parent = (QWidget*)parentWin;

    QLItem* item = [QLItem new];
    NSString* path = [NSString stringWithCString:fileToLoad encoding:NSUTF8StringEncoding];
    NSURL* url = [NSURL URLWithString:path];
    item.previewItemURL = url;

    NSView* parentView = (NSView*)parent->winId();
    CGRect frame = parentView.bounds;
    QLPreviewView* qlView = [[QLPreviewView alloc] initWithFrame:frame style:QLPreviewViewStyleNormal];
    [qlView setPreviewItem:item];
    [parentView addSubview:qlView];
    return qlView;
}

void ListCloseWindow(void* listWin) {
    [((NSView*)listWin) removeFromSuperview];
}

void /*DCPCALL*/ ListGetDetectString(char* detectString, int maxlen) {
    strncpy(detectString, "(EXT=\"*\")", maxlen);
}

}
