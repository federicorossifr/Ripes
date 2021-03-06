#pragma once

#include <QToolBar>
#include <QWidget>

namespace Ripes {

class RipesTab : public QWidget {
public:
    RipesTab(QToolBar* toolbar, QWidget* parent = nullptr) : QWidget(parent), m_toolbar(toolbar) {}
    QToolBar* getToolbar() { return m_toolbar; }

protected:
    QToolBar* m_toolbar = nullptr;
};
}  // namespace Ripes
