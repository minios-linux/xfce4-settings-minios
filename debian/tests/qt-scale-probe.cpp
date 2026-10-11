// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QPushButton button("Interface scale");
    button.setWindowTitle(QString("HiDPI Qt%1 probe").arg(QT_VERSION_MAJOR));
    button.resize(220, 60);
    if (app.arguments().contains("--stay"))
        button.move(QT_VERSION_MAJOR == 5 ? 50 : 350, 50);
    button.show();
    QTimer::singleShot(500, [&] {
        QJsonObject result;
        result["version"] = QT_VERSION_STR;
        result["scale"] = button.devicePixelRatioF();
        result["font_points"] = button.font().pointSizeF();
        result["logical_dpi"] = app.primaryScreen()->logicalDotsPerInch();
        result["logical_width"] = button.width();
        result["logical_height"] = button.height();
        result["xid"] = QString::number(button.winId());
        const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
        std::puts(json.constData());
        std::fflush(stdout);
        if (!app.arguments().contains("--stay"))
            app.quit();
    });
    QTimer::singleShot(90000, &app, &QApplication::quit);
    return app.exec();
}
