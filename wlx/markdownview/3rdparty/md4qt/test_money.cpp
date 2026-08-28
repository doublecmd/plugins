#include <iostream>
#include "md4qt/parser.h"

int main() {
    MD::Parser<MD::QStringTrait> parser;
    auto doc = parser.parse(QStringLiteral("test_money.md"));
    for (auto it = doc->items().constBegin(); it != doc->items().constEnd(); ++it) {
        if ((*it)->type() == MD::ItemType::Paragraph) {
            auto p = std::static_pointer_cast<MD::Paragraph<MD::QStringTrait>>(*it);
            for (auto i = p->items().constBegin(); i != p->items().constEnd(); ++i) {
                if ((*i)->type() == MD::ItemType::Math) {
                    auto m = std::static_pointer_cast<MD::Math<MD::QStringTrait>>(*i);
                    std::wcout << L"Math: " << m->expr().toStdWString() << std::endl;
                } else if ((*i)->type() == MD::ItemType::Text) {
                    auto t = std::static_pointer_cast<MD::Text<MD::QStringTrait>>(*i);
                    std::wcout << L"Text: " << t->text().toStdWString() << std::endl;
                }
            }
        }
    }
    return 0;
}
