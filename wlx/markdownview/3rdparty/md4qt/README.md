<!--
	SPDX-FileCopyrightText: 2022-2025 Igor Mironchik <igor.mironchik@gmail.com>
	SPDX-License-Identifier: MIT
-->

<img src="https://kde.org/stuff/clipart/logo/kde-logo-blue-transparent-source.svg" width="20%" align="right">

`md4qt` is a static C++ library for parsing Markdown.

`md4qt` supports CommonMark 0.31.2 Spec, and some GitHub extensions, such as
tables, footnotes, tasks lists, strikethroughs, LaTeX Maths injections, GitHub's auto-links.

This library parses Markdown into tree structure.

* [Example](#example)
* [License](#license)
* [Benchmark](#benchmark)
* [Playground](#playground)
* [Release notes](#release-notes)
* [What should I know about links in the document?](#what-should-i-know-about-links-in-the-document)
* [What is the second argument of `MD::Parser::parse()`?](#what-is-the-second-argument-of-mdparserparse)
* [What is an `MD::Anchor`?](#what-is-an-mdanchor)
* [Does the library throw exceptions?](#does-the-library-throw-exceptions)
* [How can I convert `MD::Document` into `HTML`?](#how-can-i-convert-mddocument-into-html)
* [How can I obtain positions of blocks/elements in `Markdown` file?](#how-can-i-obtain-positions-of-blockselements-in-markdown-file)
* [How can I easily traverse through the `MD::Document`?](#how-can-i-easily-traverse-through-the-mddocument)
* [Is it possible to find `Markdown` item by its position?](#is-it-possible-to-find-markdown-item-by-its-position)
* [How can I walk through the document and find all items of given type?](#how-can-i-walk-through-the-document-and-find-all-items-of-given-type)
* [How can I add and process a custom (user-defined) item in `MD::Document`?](#how-can-i-add-and-process-a-custom-user-defined-item-in-mddocument)

# Example

```cpp
#include <md4qt/parser.h>

int main()
{
    MD::Parser p;

    auto doc = p.parse(QStringLiteral("your_markdown.md"));

    for (auto it = doc->items().cbegin(), last = doc->items().cend(); it != last; ++it) {
        switch ((*it)->type())
        {
        case MD::ItemType::Anchor :
        {
            auto a = static_cast<MD::Anchor *> (it->get());
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

# License

```cpp
/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/
```

# Benchmark

Approximate benchmark with [cmark-gfm](https://github.com/github/cmark-gfm) says,
that Qt6 version of `md4qt` is slower ~8 times.
But you will get complete C++ tree structure of the Markdown document with all
major extensions and sugar and cherry on the cake.

| Markdown library | Result |
| --- | --- |
| [cmark-gfm](https://github.com/github/cmark-gfm) | ~0.22 ms |
| `md4qt` with `Qt6` | ~1.7 ms |
| `md4qt` with `Qt6` without `GitHub` auto-links extension | ~1.2 ms |

# Playground

You can play in action with `md4qt` in [Markdown Tools](https://github.com/igormironchik/markdown-tools). There you can find `Markdown` editor/viewer/converter to `PDF`.

And [KleverNotes](https://invent.kde.org/office/klevernotes) from `KDE` uses `md4qt` too.

# Release notes

* Note that version **5.0.0** is API incompatible with **4.x.x**. Version **5.0.0** was
fully refactored for better performance and be more user-friendly.

* Note that version **4.0.0** is API incompatible with **3.0.0**. In version **4.0.0** were
changed rules with spaces, this version fully supports CommonMark standard in this question.
Methods `isSpaceBefore()`, `isSpaceAfter()` were removed, and spaces are presented as in
Markdown, so keep it in mind.

# What should I know about links in the document?

 * In some cases in Markdown link's URL is something document related. So, when
you got a `MD::Link` in the document check if the labelled links of the
document contains key with URL in the link, and if so, use URL from
labelled links, look:

   ```cpp
   MD::Link *item = ...;

   QString url = item->url();

   const auto it = doc->labeledLinks().find(url);
   
   if (it != doc->labeledLinks().cend()) {
       url = it.value()->url();
   }
   ```

# What is the second argument of `MD::Parser::parse()`?

 * Second argument of `MD::Parser::parse()` is a flag that tells the
parser to process Markdown files recursively or not. If parsing is recursive
then if in the targeted Markdown file exist links to other Markdown files,
then they will be parsed too and will exist in the resulting document.

# What is an `MD::Anchor`?

 * As `md4qt` supports recursive Markdown parsing, then in the resulting
document can be represented more than one Markdown file. Each file in the
document starts with `MD::Anchor`, it just shows that during traversing through
the document you reached new file.

# Does the library throw exceptions?

 * No. This library doesn't use exceptions. Any text is a valid Markdown, so I
don't need to inform user about errors. Qt itself doesn't use exceptions either.
So you can catch only standard C++ exceptions, like `std::bad_alloc`, for
example.

# How can I convert `MD::Document` into `HTML`?

 * In version `2.0.5` were made commits with implementation of
`MD::toHtml()` function. You can do the following:

   ```cpp
   #include <md4qt/parser.h>
   #include <md4qt/html.h>

   int main()
   {
       MD::Parse p;

       auto doc = p.parse(QStringLiteral("your_markdown.md"));

       const auto html = MD::toHtml(doc);

       return 0;
   }
   ```

# How can I obtain positions of blocks/elements in `Markdown` file?

 * Done in version `2.0.5`. Remember that all positions in `md4qt` start with 0,
where first symbol on first line will have coordinates `(0,0)`. One more important thing is
that all ranges of position in `md4qt` are given inclusive, that mean that last column
of any element will point to the last symbol in this element.

# How can I easily traverse through the `MD::Document`?

* Since version `2.6.0` in `visitor.h` header implemented `MD::Visitor` interface
with which you can easily walk through the document, all you need is implement/override
virtual methods to handle that or another element in the document, like:

  ```cpp
  /*!
   * Handle heading.
   *
   * \a h Heading.
   */
  virtual void onHeading(Heading *h) = 0;
  ```

# Is it possible to find `Markdown` item by its position?

 * Since version `3.0.0` was added new structure `MD::PosCache`. You can pass
`MD::Document` into its `MD::PosCache::initialize()` method and find first item with all its
nested first children by given position with `MD::PosCache::findFirstInCache()` method.

# How can I walk through the document and find all items of given type?

 * Since version `3.0.0` was added algorithm `MD::forEach()`.

   ```cpp
   /*!
    * \inheaderfile md4qt/algo.h
    *
    * \brief Calls function for each item in the document with the given type.
    *
    * \a types Vector of item's types to be processed.
    *
    * \a doc Document.
    *
    * \a func Functor object.
    *
    * \a maxNestingLevel Maximun nesting level. 0 means infinity, 1 - only top level items...
    */
   inline void forEach(
       const QVector<ItemType> &types,
       QSharedPointer<Document> doc,
       ItemFunctor func,
       unsigned int maxNestingLevel = 0);
   ```

# How can I add and process a custom (user-defined) item in `MD::Document`?

 * Since version `3.0.0` in `MD::ItemType` enum appeared `MD::UserDefined` enumerator.
So you can inherit from any `MD::Item` class and return from `type()` method
value greater or equal `MD::ItemType::UserData`. To handle user-defined types of
items in `MD::Visitor` class now exists method `void onUserDefined(MD::Item *item)`.
So you can handle your custom items and do what you need.
