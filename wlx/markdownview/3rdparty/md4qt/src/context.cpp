/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "context.h"

namespace MD
{

Context::Context(Context *prnt)
    : m_parent(prnt)
{
}

Context::~Context() = default;

void Context::applyParentContext(Context &ctx)
{
    setIndentColumn(ctx.lastChildIndent());
    m_parent = &ctx;
}

void Context::updateParentContextForAllChildren()
{
    for (auto it = m_children.begin(), last = m_children.end(); it != last; ++it) {
        it->applyParentContext(*this);
        it->updateParentContextForAllChildren();
    }
}

} /* namespace MD */
