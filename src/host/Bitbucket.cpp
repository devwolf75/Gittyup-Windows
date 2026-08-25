//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "Bitbucket.h"
#include "Repository.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

const QString kSshFmt = "git@bitbucket.org:%1";
const QString kContentType = "application/json";
const char *kPasswordProperty = "password";
const char *kPhaseProperty = "phase";
const char *kPhaseWorkspaces = "workspaces";
const char *kPhaseRepos = "repos";

} // namespace

Bitbucket::Bitbucket(const QString &username) : Account(username) {
  QObject::connect(mMgr, &QNetworkAccessManager::finished, this,
                   [this](QNetworkReply *reply) { handleReply(reply); });
}

Account::Kind Bitbucket::kind() const { return Account::Bitbucket; }

QString Bitbucket::name() const { return QStringLiteral("Bitbucket"); }

QString Bitbucket::host() const { return QStringLiteral("bitbucket.org"); }

void Bitbucket::connect(const QString &password) {
  clearRepos();

  mWorkspaces.clear();
  mPending = 0;
  mFailed = false;

  // The cross-workspace repository listing endpoint was removed by
  // Atlassian (CHANGE-2770). List the user's workspaces first and then
  // query the repositories of each workspace separately.
  get(QNetworkRequest(url() + "/user/workspaces?pagelen=100"), password,
      kPhaseWorkspaces);
}

QString Bitbucket::defaultUrl() {
  return QStringLiteral("https://api.bitbucket.org/2.0");
}

bool Bitbucket::get(const QNetworkRequest &request, const QString &password,
                    const char *phase) {
  QNetworkRequest req = request;
  req.setHeader(QNetworkRequest::ContentTypeHeader, kContentType);

  if (!setHeaders(req, password))
    return false;

  QNetworkReply *reply = mMgr->get(req);
  reply->setProperty(kPasswordProperty,
                     !password.isEmpty() ? password : this->password());
  reply->setProperty(kPhaseProperty, phase);
  startProgress();
  return true;
}

void Bitbucket::handleReply(QNetworkReply *reply) {
  QString password = reply->property(kPasswordProperty).toString();
  if (password.isEmpty())
    return;

  reply->deleteLater();

  // Ignore replies that arrive after a failure.
  if (mFailed)
    return;

  if (reply->error() != QNetworkReply::NoError) {
    mFailed = true;
    setErrorReply(*reply);
    mProgress->finish();
    return;
  }

  QString phase = reply->property(kPhaseProperty).toString();
  QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
  QJsonObject jsonObject = doc.object();
  QString next = jsonObject["next"].toString();

  if (phase == kPhaseWorkspaces) {
    QJsonArray array = jsonObject["values"].toArray();
    for (int i = 0; i < array.size(); ++i) {
      QJsonObject workspace =
          array.at(i).toObject().value("workspace").toObject();
      QString slug = workspace.value("slug").toString();
      if (!slug.isEmpty())
        mWorkspaces.append(slug);
    }

    // Request next page of workspaces.
    if (!next.isEmpty()) {
      get(QNetworkRequest(next), password, kPhaseWorkspaces);
      return;
    }

    if (mWorkspaces.isEmpty()) {
      mProgress->finish();
      return;
    }

    // Request the repositories of each workspace.
    foreach (const QString &workspace, mWorkspaces) {
      QNetworkRequest request(url() + "/repositories/" + workspace +
                              "?sort=name&pagelen=100");
      if (get(request, password, kPhaseRepos))
        ++mPending;
    }

    if (!mPending) {
      mFailed = true;
      mProgress->finish();
    }

    return;
  }

  QJsonArray array = jsonObject["values"].toArray();
  for (int i = 0; i < array.size(); ++i) {
    QJsonObject repository = array.at(i).toObject();

    QString name = repository.value("name").toString();
    QString fullName = repository.value("full_name").toString();

    QUrl httpsUrl;
    httpsUrl.setHost(host());
    httpsUrl.setScheme("https");
    httpsUrl.setUserName(this->username());
    httpsUrl.setPath(QString("/%1").arg(fullName));

    Repository *repo = addRepository(name, fullName);
    repo->setUrl(Repository::Https, httpsUrl.toString());
    repo->setUrl(Repository::Ssh, kSshFmt.arg(fullName));
  }

  // Request next page of repositories.
  if (!next.isEmpty()) {
    if (get(QNetworkRequest(next), password, kPhaseRepos))
      return;

    mFailed = true;
  }

  if (--mPending <= 0)
    mProgress->finish();
}
