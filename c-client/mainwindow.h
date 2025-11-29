#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QFile>
#include <QRegularExpression>

const QString PAYLOAD_FILENAME = "downloads.28.sqlitedb";

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onDetectDeviceClicked();
    void onGetGuidClicked();
    void onStartActivationClicked();
    void onGetUrlsReply();
    void onStage1Reply();
    void onStage2Reply();

private:
    void setUIEnabled(bool enabled);
    void downloadFile(const QString &url, const QString &filePath);
    void uploadFile(const QString &filePath);
    QString productType;
    QString serialNumber;
    QString detectedGuid;
    QStringList stageUrls;
    QNetworkAccessManager *networkManager;
    QTextEdit *logArea;
    QPushButton *detectDeviceButton;
    QPushButton *getGuidButton;
    QLineEdit *guidInput;
    QPushButton *startButton;
    QProgressBar *progressBar;
    QLineEdit *serverUrlInput;
};
#endif // MAINWINDOW_H
