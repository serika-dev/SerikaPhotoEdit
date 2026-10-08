#include "actions/ActionRunner.h"
#include "io/FormatIO.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QColorSpace>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <memory>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif
using namespace serika;
static int batch(const QStringList &args) {
    QTextStream output(stdout), errors(stderr);
    if (args.size() != 3) {
        errors << "Usage: SerikaPhotoEdit --batch action.json input-folder output-folder\n";
        return 2;
    }
    QJsonArray steps;
    QString actionError;
    if (!ActionRunner::load(args[0], &steps, &actionError)) {
        errors << actionError << '\n';
        return 2;
    }
    QDir input(args[1]);
    QDir().mkpath(args[2]);
    int failures = 0;
    for (const auto &entry : input.entryInfoList(QDir::Files)) {
        QString error;
        std::unique_ptr<Document> doc(FormatIO::open(entry.absoluteFilePath(), &error));
        if (!doc) {
            errors << entry.fileName() << ": " << error << '\n';
            failures++;
            continue;
        }
        if (!ActionRunner::run(doc.get(), steps, &error)) {
            errors << entry.fileName() << ": " << error << '\n';
            failures++;
            continue;
        }
        QString target = QDir(args[2]).filePath(entry.completeBaseName() + ".spe");
        if (!FormatIO::saveNative(doc.get(), target, &error)) {
            errors << error << '\n';
            failures++;
        } else
            output << target << '\n';
    }
    return failures ? 1 : 0;
}
int main(int argc, char **argv) {
    bool headless = false;
    for (int i = 1; i < argc; i++)
        if (QString::fromLocal8Bit(argv[i]) == "--batch")
            headless = true;
    if (headless && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
    if (headless && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        const QString systemFonts = QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts");
        if (QDir(systemFonts).exists())
            qputenv("QT_QPA_FONTDIR", QFile::encodeName(systemFonts));
    }
#endif
    QApplication app(argc, argv);
    app.setApplicationName("SerikaPhotoEdit");
    app.setApplicationVersion("0.0.1");
    app.setOrganizationName("Serika");
    app.setOrganizationDomain("serika.app");
    app.setStyle("Fusion");
    QCommandLineParser parser;
    parser.setApplicationDescription("Serika PhotoEdit — native layer-based image editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"demo", "Open the original welcome project"});
    parser.addOption({"screenshot", "Save a window capture and exit", "path"});
    parser.addOption(
        {"batch", "Apply an action to a folder; provide action, input and output positional arguments"});
    parser.addPositionalArgument("files", "Documents to open, or batch arguments");
    parser.process(app);
    if (parser.isSet("batch"))
        return batch(parser.positionalArguments());
    MainWindow window;
    window.show();
#ifdef Q_OS_WIN
    BOOL dark = TRUE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(window.winId()), 20, &dark, sizeof(dark));
#endif
    for (const auto &path : parser.positionalArguments())
        window.openFile(QFileInfo(path).absoluteFilePath());
    if (parser.isSet("demo"))
        window.openDemo();
    if (parser.isSet("screenshot")) {
        auto path = parser.value("screenshot");
        QTimer::singleShot(1500, &window, [&window, path, &app] {
            window.grab().save(path);
            app.quit();
        });
    }
    return app.exec();
}
