//
//          Copyright (c) 2018, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "GitCredential.h"
#include "qtsupport.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QUrl>

namespace {

QString host(const QString &url) {
  QString host = QUrl(url).host();
  if (!host.isEmpty())
    return host;

  // Extract hostname from SSH URL.
  int end = url.indexOf(':');
  int begin = url.indexOf('@') + 1;
  return url.mid(begin, end - begin);
}

QString protocol(const QString &url) {
  QString scheme = QUrl(url).scheme();
  return !scheme.isEmpty() ? scheme : "ssh";
}

} // namespace

GitCredential::GitCredential(const QString &name) : mName(name) {}

bool GitCredential::get(const QString &url, QString &username,
                        QString &password) {
  QProcess process;
  process.start(command(), {"get"});
  if (!process.waitForStarted())
    return false;

  QTextStream out(&process);
  out << "protocol=" << protocol(url) << Qt::endl;
  out << "host=" << host(url) << Qt::endl;
  if (!username.isEmpty())
    out << "username=" << username << Qt::endl;
  out << Qt::endl;

  process.closeWriteChannel();
  process.waitForFinished();

  QString output = process.readAllStandardOutput();
  foreach (const QString &line, output.split('\n')) {
    int pos = line.indexOf('=');
    if (pos < 0)
      continue;

    QString key = line.left(pos);
    QString value = line.mid(pos + 1);
    if (key == "username") {
      username = value;
    } else if (key == "password") {
      password = value;
    }
  }

  return !username.isEmpty() && !password.isEmpty();
}

bool GitCredential::store(const QString &url, const QString &username,
                          const QString &password) {
  QProcess process;
  process.start(command(), {"store"});
  if (!process.waitForStarted())
    return false;

  QTextStream out(&process);
  out << "protocol=" << protocol(url) << Qt::endl;
  out << "host=" << host(url) << Qt::endl;
  out << "username=" << username << Qt::endl;
  out << "password=" << password << Qt::endl;
  out << Qt::endl;

  process.closeWriteChannel();
  process.waitForFinished();

  return true;
}

QString GitCredential::command() const {
  QString name = QString("git-credential-%1").arg(mName);
  QDir appDir = QCoreApplication::applicationDirPath();
  appDir.cd("credential-helpers");

  // Prefer credential helpers directly installed into Gittyup's app dir
  QString candidate =
      QStandardPaths::findExecutable(name, QStringList(appDir.path()));
  if (!candidate.isEmpty()) {
    return candidate;
  }

  candidate = QStandardPaths::findExecutable(name);
  if (!candidate.isEmpty()) {
    return candidate;
  }

#ifdef Q_OS_WIN
  // Look for the GIT CLI installation path. Iterate over all PATH entries
  // instead of using the first git.exe found: it may be a shim (e.g. Scoop)
  // whose directory does not contain the credential helpers.
  const QStringList pathEntries =
      QProcessEnvironment::systemEnvironment()
          .value(QStringLiteral("PATH"))
          .split(QLatin1Char(';'), Qt::SkipEmptyParts);

  for (const QString &entry : pathEntries) {
    QString gitPath =
        QStandardPaths::findExecutable(QStringLiteral("git"), {entry});
    if (gitPath.isEmpty())
      continue;

    QDir gitDir = QFileInfo(gitPath).dir();
    if (gitDir.dirName() != QLatin1String("cmd") &&
        gitDir.dirName() != QLatin1String("bin")) {
      continue;
    }

    gitDir.cdUp();

#ifdef Q_OS_WIN64
    gitDir.cd(QStringLiteral("mingw64"));
#else
    gitDir.cd(QStringLiteral("mingw32"));
#endif

    // Newer Git for Windows versions may install the credential helpers
    // into libexec/git-core instead of bin.
    for (const char *subdir : {"bin", "libexec/git-core"}) {
      QDir helperDir(gitDir);
      if (!helperDir.cd(QLatin1String(subdir)))
        continue;

      candidate = QStandardPaths::findExecutable(name, {helperDir.path()});
      if (!candidate.isEmpty())
        return candidate;
    }
  }
#endif

  return name;
}
