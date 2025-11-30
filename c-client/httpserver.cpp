#include "httpserver.h"
#include <QTcpSocket>
#include <QDebug>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QProcess>
#include <QRandomGenerator>

bool createZip(const QString &dirPath, const QString &zipPath)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("zip", QStringList() << "-r" << "-0" << zipPath << ".");
    process.setWorkingDirectory(dirPath);
    if (!process.waitForFinished()) {
        qDebug() << "Error creating zip file: " << process.errorString();
        return false;
    }
    return true;
}

bool createSQLiteFromDump(const QString &sqlDump, const QString &outputFile)
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(outputFile);
    if (!db.open()) {
        qDebug() << "Error: connection with database failed";
        return false;
    }

    QSqlQuery query;
    QStringList statements = sqlDump.split(';');
    foreach (const QString &statement, statements) {
        if (statement.trimmed().isEmpty()) continue;
        if (!query.exec(statement)) {
            qDebug() << "Error executing statement: " << statement;
            qDebug() << query.lastError();
            db.close();
            return false;
        }
    }

    db.close();
    return true;
}

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent)
{
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(socketDescriptor);

    connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &HttpServer::onDisconnected);

    qDebug() << "New connection";
}

void HttpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    handleRequest(socket, socket->readAll());
}

void HttpServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    qDebug() << "Disconnected";
    socket->deleteLater();
}

QString generateRandomName(int length = 16) {
    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    const int randomStringLength = length;

    QString randomString;
    for(int i=0; i<randomStringLength; ++i)
    {
        int index = QRandomGenerator::global()->bounded(possibleCharacters.length());
        QChar nextChar = possibleCharacters.at(index);
        randomString.append(nextChar);
    }
    return randomString;
}

void HttpServer::handleRequest(QTcpSocket *socket, const QByteArray &requestData)
{
    qDebug() << "Handling request";
    qDebug() << requestData;

    QString requestString(requestData);
    QStringList requestLines = requestString.split("\r\n");
    QStringList requestLine = requestLines[0].split(" ");

    if (requestLine.size() < 2) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    QString method = requestLine[0];
    QString path = requestLine[1];

    if (method != "GET") {
        socket->write("HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    QUrl url(path);
    QUrlQuery query(url);

    QString prd = query.queryItemValue("prd");
    QString guid = query.queryItemValue("guid");
    QString sn = query.queryItemValue("sn");

    if (prd.isEmpty() || guid.isEmpty() || sn.isEmpty()) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\nMissing prd, guid, or sn");
        socket->disconnectFromHost();
        return;
    }

    QString prdFormatted = prd;
    prdFormatted.replace(",", "-");

    QString plistPath = ":/Maker/" + prdFormatted + "/com.apple.MobileGestalt.plist";
    if (!QFile::exists(plistPath)) {
        socket->write("HTTP/1.1 500 Internal Server Error\r\n\r\nPlist not found");
        socket->disconnectFromHost();
        return;
    }

    QString tempPath = QDir::tempPath();
    QString randomName1 = generateRandomName();
    QString firstStepDir = tempPath + "/" + randomName1;
    QDir().mkdir(firstStepDir);

    QString cachesDir = firstStepDir + "/Caches";
    QDir().mkdir(cachesDir);

    QFile mimeTypeFile(cachesDir + "/mimetype");
    mimeTypeFile.open(QIODevice::WriteOnly);
    mimeTypeFile.write("application/epub+zip");
    mimeTypeFile.close();

    QFile plistFile(":/Maker/" + prdFormatted + "/com.apple.MobileGestalt.plist");
    plistFile.copy(cachesDir + "/com.apple.MobileGestalt.plist");

    QString zipPath = firstStepDir + "/temp.zip";
    if (!createZip(firstStepDir, zipPath)) {
        socket->write("HTTP/1.1 500 Internal Server Error\r\n\r\nFailed to create zip file");
        socket->disconnectFromHost();
        return;
    }

    QFile::rename(zipPath, firstStepDir + "/fixedfile");

    QString fixedFileUrl = "http://127.0.0.1:8080/" + randomName1 + "/fixedfile";

    QString randomName2 = generateRandomName();
    QString secondStepDir = tempPath + "/" + randomName2;
    QDir().mkdir(secondStepDir);

    QFile blDumpFile(":/BLDatabaseManager.png");
    blDumpFile.open(QIODevice::ReadOnly);
    QString blDump = blDumpFile.readAll();
    blDump.replace("KEYOOOOOO", fixedFileUrl);

    QString blSqlite = secondStepDir + "/BLDatabaseManager.sqlite";
    if (!createSQLiteFromDump(blDump, blSqlite)) {
        socket->write("HTTP/1.1 500 Internal Server Error\r\n\r\nFailed to create BLDatabaseManager.sqlite");
        socket->disconnectFromHost();
        return;
    }
    QFile::rename(blSqlite, secondStepDir + "/belliloveu.png");
    QString blUrl = "http://127.0.0.1:8080/" + randomName2 + "/belliloveu.png";

    QString randomName3 = generateRandomName();
    QString lastStepDir = tempPath + "/" + randomName3;
    QDir().mkdir(lastStepDir);

    QFile dlDumpFile(":/downloads.28.png");
    dlDumpFile.open(QIODevice::ReadOnly);
    QString dlDump = dlDumpFile.readAll();
    dlDump.replace("https://google.com", blUrl);
    dlDump.replace("GOODKEY", guid);

    QString finalDb = lastStepDir + "/downloads.sqlitedb";
    if (!createSQLiteFromDump(dlDump, finalDb)) {
        socket->write("HTTP/1.1 500 Internal Server Error\r\n\r\nFailed to create downloads.sqlitedb");
        socket->disconnectFromHost();
        return;
    }
    QFile::rename(finalDb, lastStepDir + "/apllefuckedhhh.png");
    QString finalUrl = "http://127.0.0.1:8080/" + randomName3 + "/apllefuckedhhh.png";

    QJsonObject response;
    response["success"] = true;
    QJsonObject links;
    links["step1_fixedfile"] = fixedFileUrl;
    links["step2_bldatabase"] = blUrl;
    links["step3_final"] = finalUrl;
    response["links"] = links;

    QJsonDocument doc(response);
    QByteArray jsonData = doc.toJson();

    socket->write("HTTP/1.1 200 OK\r\n");
    socket->write("Content-Type: application/json\r\n");
    socket->write("Content-Length: " + QByteArray::number(jsonData.size()) + "\r\n");
    socket->write("\r\n");
    socket->write(jsonData);
    socket->disconnectFromHost();
}
