// line_number_area.h —— 行号区：Qt 官方 CodeEditor 模式。
// 视觉透明但接收鼠标事件；定义在 line_number_area.cpp（需 Editor 完整类型）。
#pragma once

#include <QWidget>

class Editor;

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(Editor *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Editor *m_editor;
};

