#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QInputDialog>
#include <string>
#include <QSettings>
#include "include/tools/CommandLineTool.h"
#include "include/tools/dbmanager.h"


void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if(MainWindow::s_textEdit == 0)
    {
        QByteArray localMsg = msg.toLocal8Bit();
        switch (type) {
        case QtDebugMsg:
            fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            break;
        case QtWarningMsg:
            fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            break;
        case QtCriticalMsg:
            fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            break;
        case QtFatalMsg:
            fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            abort();
        }
    }
    else
    {
        // redundant check, could be removed, or the
        // upper if statement could be removed
        if(MainWindow::s_textEdit != 0){
            MainWindow::s_textEdit->log_with_signal(msg);
            MainWindow::s_textEdit->update();
        }
    }
}

int main(int argc, char *argv[])
{
    // Check for --console flag before creating QApplication
    bool console_mode = false;
    string mode = "holdem";
    string resource_dir = "./resources";
    string input_file;

    for(int i = 1; i < argc; i++) {
        string arg = argv[i];
        if(arg == "--console" || arg == "-c") {
            console_mode = true;
        } else if(arg == "--mode" || arg == "-m") {
            if(i + 1 < argc) mode = argv[++i];
        } else if(arg == "--resource-dir" || arg == "-r") {
            if(i + 1 < argc) resource_dir = argv[++i];
        } else if(arg == "--input" || arg == "-i") {
            if(i + 1 < argc) input_file = argv[++i];
        } else if(arg == "--help" || arg == "-h") {
            cout << "TexasSolverGui - Poker Solver" << endl;
            cout << "Usage: ./TexasSolverGui [options]" << endl;
            cout << "Options:" << endl;
            cout << "  --console, -c          Run in console mode (no GUI)" << endl;
            cout << "  --mode, -m <mode>      Set mode: holdem or shortdeck (default: holdem)" << endl;
            cout << "  --resource-dir, -r <dir>  Set resource directory (default: ./resources)" << endl;
            cout << "  --input, -i <file>     Execute commands from file (console mode)" << endl;
            cout << "  --help, -h             Show this help" << endl;
            return 0;
        }
    }

    if(console_mode) {
        QCoreApplication* a = new QCoreApplication(argc, argv);
        cout << "=== TexasSolver Console Mode ===" << endl;
        cout << "Mode: " << mode << endl;
        cout << "Resource dir: " << resource_dir << endl;
        CommandLineTool clt = CommandLineTool(mode, resource_dir);
        if(!input_file.empty()) {
            cout << "Executing from file: " << input_file << endl;
            clt.execFromFile(input_file);
        } else {
            cout << "Type commands (e.g. set_pot, build_tree, start_solve, print_node_ids)" << endl;
            cout << "Press Ctrl+D to exit." << endl;
            clt.startWorking();
        }
        return 0;
    }

    qInstallMessageHandler(myMessageOutput);
    QApplication a(argc, argv);

    QSettings setting("TexasSolver", "Setting");
    setting.beginGroup("solver");
    QString language_str = setting.value("language").toString();
    QTranslator trans;

    if(language_str == ""){
        QStringList languages;
        languages << "English" << QString::fromLocal8Bit("简体中文");
        QString lang = QInputDialog::getItem(NULL,"select language","language",languages,0,false);

        if(lang == "English"){
            (void)trans.load(":/lang_en.qm");
            language_str = "EN";
        }else if(lang == QString::fromLocal8Bit("简体中文")){
            (void)trans.load(":/lang_cn.qm");
            language_str = "CN";
        }
        a.installTranslator(&trans);
        setting.setValue("language",language_str);
    }else{
        if(language_str == "EN"){
            trans.load(":/lang_en.qm");
        }else if(language_str == "CN"){
            trans.load(":/lang_cn.qm");
        }
        a.installTranslator(&trans);
    }

    int dump_round = setting.value("dump_round").toInt();
    if(dump_round == 0){
        setting.setValue("dump_round",2);
    }

    DBManager::instance().init();

    MainWindow w;
    w.show();

    return a.exec();
}
