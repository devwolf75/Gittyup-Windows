//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#ifndef BITBUCKET_H
#define BITBUCKET_H

#include "Account.h"
#include <QStringList>

class Bitbucket : public Account {
  Q_OBJECT

public:
  Bitbucket(const QString &username);

  Kind kind() const override;
  QString name() const override;
  QString host() const override;
  void connect(const QString &password = QString()) override;

  static QString defaultUrl();

private:
  bool get(const QNetworkRequest &request, const QString &password,
           const char *phase);
  void handleReply(QNetworkReply *reply);

  QStringList mWorkspaces;
  int mPending = 0;
  bool mFailed = false;
};

#endif
