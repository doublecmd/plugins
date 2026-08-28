<!--
	SPDX-FileCopyrightText: 2022-2025 Igor Mironchik <igor.mironchik@gmail.com>
	SPDX-License-Identifier: MIT
-->

# Converter of Markdown to QDoc

This is a tool to convert Markdown file to QDoc format with that restrictions that QDoc
doesn't support (raw HTML injections, for example, etc.).

This tool is clever enough to handle links properly...

This tool gets input Markdown file names as positional arguments.

There are three options to this tool

| Option | Description |
| --- | --- |
|`-r, --recursive` | tells to load all linked Markdown files inside the given and make corresponding QDoc files for each processed Markdown file. |
| `-o, --out <dir>` | tells where to store generated QDoc files. |
| `--offset <int>` | offset used for nested blocks |
| `--prefx <str>` | prefix used to create top-level targets. |
| `-i, --gen-includes` | tells to generate QDoc includes only, i.e. without `/*! */` comment. |

# TODO

* Formatting of generated QDoc is not very well formed now, but can be done if it needed.

# Example

A following Markdown file (`example.md`)

````md
`md4qt` is a header-only C++ library for parsing Markdown.

`md4qt` supports CommonMark 0.31.2 Spec, and some GitHub extensions, such as
tables, footnotes, tasks lists, strikethroughs, LaTeX Maths injections, GitHub's auto-links.

`md4qt` can be built with Qt6 or with ICU.

This library parses Markdown into tree structure.

* [Example](#example)

# Example

```cpp
#define MD4QT_QT_SUPPORT
#include <md4qt/parser.hpp>

int main()
{
    MD::Parser<MD::QStringTrait> p;

    auto doc = p.parse(QStringLiteral("your_markdown.md"));

    for (auto it = doc->items().cbegin(), last = doc->items().cend(); it != last; ++it) {
        switch ((*it)->type())
        {
        case MD::ItemType::Anchor :
        {
            auto a = static_cast<MD::Anchor<MD::QStringTrait>*> (it->get());
            qDebug() << a->label();
        }
            break;

        default :
            break;
        }
    }

    return 0;
}
```
````

will generate a following QDoc file

```qdoc
/*!
    \page example.md.html
    \c {md4qt} is a header-only C++ library for parsing Markdown.

    \c {md4qt} supports CommonMark 0.31.2 Spec, and some GitHub extensions, such as
    tables, footnotes, tasks lists, strikethroughs, LaTeX Maths injections, GitHub's auto-links.

    \c {md4qt} can be built with Qt6 or with ICU.

    This library parses Markdown into tree structure.


    \list
        \li \l {#example//home/igor/Work/KDE/md2qdoc/build/Desktop-Debug/bin/example.md} {Example}
    \endlist

    \target #example//home/igor/Work/KDE/md2qdoc/build/Desktop-Debug/bin/example.md
    \section1 Example

    \code /
    #define MD4QT_QT_SUPPORT
    #include <md4qt\1parser.hpp>
    
    int main()
    {
        MD::Parser<MD::QStringTrait> p;
    
        auto doc = p.parse(QStringLiteral("your_markdown.md"));
    
        for (auto it = doc->items().cbegin(), last = doc->items().cend(); it != last; ++it) {
            switch ((*it)->type())
            {
            case MD::ItemType::Anchor :
            {
                auto a = static_cast<MD::Anchor<MD::QStringTrait>*> (it->get());
                qDebug() << a->label();
            }
                break;
    
            default :
                break;
            }
        }
    
        return 0;
    }
    \endcode


*/
```
