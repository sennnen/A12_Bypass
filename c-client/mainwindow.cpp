#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Create a central widget
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // Create a main vertical layout
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Create and configure the log area
    logArea = new QTextEdit(this);
    logArea->setReadOnly(true);
    mainLayout->addWidget(logArea);

    // Create a horizontal layout for the buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    // Create and configure the buttons
    detectDeviceButton = new QPushButton("Detect Device", this);
    getGuidButton = new QPushButton("Get GUID Automatically", this);
    startButton = new QPushButton("Start Activation", this);

    buttonLayout->addWidget(detectDeviceButton);
    buttonLayout->addWidget(getGuidButton);
    buttonLayout->addWidget(startButton);

    mainLayout->addLayout(buttonLayout);

    // Connect the button's clicked signal to the slot
    connect(detectDeviceButton, &QPushButton::clicked, this, &MainWindow::onDetectDeviceClicked);
    connect(getGuidButton, &QPushButton::clicked, this, &MainWindow::onGetGuidClicked);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::onStartActivationClicked);

    // Create a horizontal layout for the GUID input
    QHBoxLayout *guidLayout = new QHBoxLayout();

    // Create and configure the GUID input
    guidInput = new QLineEdit(this);
    guidInput->setPlaceholderText("Enter GUID Manually");
    guidLayout->addWidget(guidInput);

    mainLayout->addLayout(guidLayout);

    // Create and configure the progress bar
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);

    // Create a horizontal layout for the server URL input
    QHBoxLayout *serverUrlLayout = new QHBoxLayout();

    // Create and configure the server URL input
    serverUrlInput = new QLineEdit(this);
    serverUrlInput->setPlaceholderText("Enter Server URL");
    serverUrlInput->setText("http://127.0.0.1:8080/");
    serverUrlInput->hide();
    serverUrlLayout->addWidget(serverUrlInput);

    mainLayout->addLayout(serverUrlLayout);

    // Initialize the network manager and HTTP server
    networkManager = new QNetworkAccessManager(this);
    httpServer = new HttpServer(this);
    if (!httpServer->listen(QHostAddress::LocalHost, 8080)) {
        logArea->append("Failed to start server.");
    } else {
        logArea->append("Server started on port 8080.");
    }
}

MainWindow::~MainWindow()
{
}

void MainWindow::setUIEnabled(bool enabled)
{
    detectDeviceButton->setEnabled(enabled);
    getGuidButton->setEnabled(enabled);
    startButton->setEnabled(enabled);
    guidInput->setEnabled(enabled);
    serverUrlInput->setEnabled(enabled);
}

void MainWindow::onDetectDeviceClicked()
{
    logArea->clear();
    logArea->append("Detecting device...");
    setUIEnabled(false);

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        QString output = process->readAllStandardOutput();
        logArea->append(output);

        if (output.contains("ERROR: No device found")) {
            logArea->append("Device detection failed: No device found.");
            return;
        }

        QStringList lines = output.split("\n");
        for (const QString &line : lines) {
            QStringList parts = line.split(": ");
            if (parts.size() == 2) {
                QString key = parts[0].trimmed();
                QString value = parts[1].trimmed();
                if (key == "ProductType") {
                    productType = value;
                } else if (key == "SerialNumber") {
                    serialNumber = value;
                }
            }
        }
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit) {
            logArea->append("Process crashed.");
        } else if (exitCode != 0) {
            logArea->append(QString("Process failed with exit code %1.").arg(exitCode));
        } else {
            logArea->append("Device detection finished.");
            logArea->append("Product Type: " + productType);
            logArea->append("Serial Number: " + serialNumber);
        }
        setUIEnabled(true);
        process->deleteLater();
    });

    process->start("ideviceinfo", QStringList());
}

void MainWindow::onGetGuidClicked()
{
    logArea->clear();
    logArea->append("Getting GUID automatically...");
    setUIEnabled(false);

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        logArea->append(process->readAllStandardOutput());
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit) {
            logArea->append("Process crashed.");
        } else if (exitCode != 0) {
            logArea->append(QString("Process failed with exit code %1.").arg(exitCode));
        } else {
            logArea->append("GUID detection finished.");
            QDir logDir("guid.logarchive");
            if (!logDir.exists()) {
                logArea->append("Could not find guid.logarchive directory.");
                setUIEnabled(true);
                process->deleteLater();
                return;
            }

            QFile logFile(logDir.filePath("logdata.LiveData.tracev3"));
            if (!logFile.open(QIODevice::ReadOnly)) {
                logArea->append("Could not open logdata.LiveData.tracev3.");
                setUIEnabled(true);
                process->deleteLater();
                return;
            }

            QByteArray logData = logFile.readAll();
            logFile.close();

            int blManagerIndex = logData.indexOf("BLDatabaseManager");
            if (blManagerIndex == -1) {
                logArea->append("Could not find BLDatabaseManager in log file.");
                setUIEnabled(true);
                process->deleteLater();
                return;
            }

            QRegularExpression re("[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}");
            QRegularExpressionMatch match = re.match(QString(logData.mid(blManagerIndex - 1024, 2048)));
            if (match.hasMatch()) {
                detectedGuid = match.captured(0);
                logArea->append("Detected GUID: " + detectedGuid);
                guidInput->setText(detectedGuid);
            } else {
                logArea->append("Could not automatically detect GUID.");
            }
            logDir.removeRecursively();
        }
        setUIEnabled(true);
        process->deleteLater();
    });

    process->start("pymobiledevice3", QStringList() << "syslog" << "collect" << "guid.logarchive");
}

void MainWindow::onStartActivationClicked()
{
    logArea->clear();
    logArea->append("Starting activation...");
    progressBar->setValue(0);
    setUIEnabled(false);

    QString guid = guidInput->text();
    if (guid.isEmpty()) {
        guid = detectedGuid;
    }

    if (guid.isEmpty()) {
        logArea->append("GUID is not available. Please detect it automatically or enter it manually.");
        setUIEnabled(true);
        return;
    }

    if (productType.isEmpty() || serialNumber.isEmpty()) {
        logArea->append("Device information is not available. Please detect the device first.");
        setUIEnabled(true);
        return;
    }

    QUrl url(serverUrlInput->text());
    QUrlQuery query;
    query.addQueryItem("prd", productType);
    query.addQueryItem("guid", guid);
    query.addQueryItem("sn", serialNumber);
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &MainWindow::onGetUrlsReply);
}

void MainWindow::downloadFile(const QString &url, const QString &filePath)
{
    logArea->append("Downloading final payload from " + url);

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, filePath]() {
        if (reply->error()) {
            logArea->append("Download failed: " + reply->errorString());
            setUIEnabled(true);
            reply->deleteLater();
            return;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            logArea->append("Could not open file for writing: " + filePath);
            setUIEnabled(true);
            reply->deleteLater();
            return;
        }

        file.write(reply->readAll());
        file.close();

        logArea->append("Download finished. Payload saved to " + filePath);
        uploadFile(filePath);

        reply->deleteLater();
    });
}

void MainWindow::uploadFile(const QString &filePath)
{
    logArea->append("Uploading payload to device...");

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit) {
            logArea->append("Upload process crashed.");
        } else if (exitCode != 0) {
            logArea->append(QString("Upload failed with exit code %1.").arg(exitCode));
        } else {
            logArea->append("Payload uploaded successfully.");
            progressBar->setValue(100);
        }
        setUIEnabled(true);
        process->deleteLater();
    });

    process->start("pymobiledevice3", QStringList() << "afc" << "push" << filePath << "/Downloads/" + PAYLOAD_FILENAME);
}

void MainWindow::onGetUrlsReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error()) {
        logArea->append("Network error: " + reply->errorString());
        setUIEnabled(true);
        reply->deleteLater();
        return;
    }

    QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());

    if (jsonResponse.isNull() || !jsonResponse.isObject()) {
        logArea->append("Invalid JSON response from server.");
        setUIEnabled(true);
        reply->deleteLater();
        return;
    }

    QJsonObject jsonObject = jsonResponse.object();

    if (jsonObject.contains("success") && jsonObject["success"].isBool() && jsonObject["success"].toBool()) {
        if (!jsonObject.contains("links") || !jsonObject["links"].isObject()) {
            logArea->append("Invalid 'links' object in JSON response.");
            setUIEnabled(true);
            reply->deleteLater();
            return;
        }
        QJsonObject links = jsonObject["links"].toObject();

        if (!links.contains("step1_fixedfile") || !links["step1_fixedfile"].isString() ||
            !links.contains("step2_bldatabase") || !links["step2_bldatabase"].isString() ||
            !links.contains("step3_final") || !links["step3_final"].isString()) {
            logArea->append("Missing or invalid URL in 'links' object.");
            setUIEnabled(true);
            reply->deleteLater();
            return;
        }

        stageUrls.clear();
        stageUrls.append(links["step1_fixedfile"].toString());
        stageUrls.append(links["step2_bldatabase"].toString());
        stageUrls.append(links["step3_final"].toString());

        logArea->append("Stage 1 URL: " + stageUrls[0]);
        logArea->append("Stage 2 URL: " + stageUrls[1]);
        logArea->append("Stage 3 URL: " + stageUrls[2]);

        progressBar->setValue(25);
        logArea->append("Pre-loading stage 1...");
        QNetworkRequest request(stageUrls[0]);
        QNetworkReply *reply1 = networkManager->get(request);
        connect(reply1, &QNetworkReply::finished, this, &MainWindow::onStage1Reply);
    } else {
        logArea->append("Server returned an error: " + jsonObject["error"].toString());
        setUIEnabled(true);
    }
    reply->deleteLater();
}

void MainWindow::onStage1Reply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error()) {
        logArea->append("Stage 1 pre-load failed: " + reply->errorString());
        setUIEnabled(true);
        reply->deleteLater();
        return;
    }
    progressBar->setValue(50);
    logArea->append("Pre-loading stage 2...");
    QNetworkRequest request(stageUrls[1]);
    QNetworkReply *reply2 = networkManager->get(request);
    connect(reply2, &QNetworkReply::finished, this, &MainWindow::onStage2Reply);
    reply->deleteLater();
}

void MainWindow::onStage2Reply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error()) {
        logArea->append("Stage 2 pre-load failed: " + reply->errorString());
        setUIEnabled(true);
        reply->deleteLater();
        return;
    }
    progressBar->setValue(75);
    downloadFile(stageUrls[2], PAYLOAD_FILENAME);
    reply->deleteLater();
}
