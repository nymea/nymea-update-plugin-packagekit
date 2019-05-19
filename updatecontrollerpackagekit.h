/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2019 Michael Zanetti <michael.zanetti@nymea.io>          *
 *                                                                         *
 *  This file is part of nymea.                                            *
 *                                                                         *
 *  nymea is free software: you can redistribute it and/or modify          *
 *  it under the terms of the GNU General Public License as published by   *
 *  the Free Software Foundation, version 2 of the License.                *
 *                                                                         *
 *                                                                         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *  GNU General Public License for more details.                           *
 *                                                                         *
 *  You should have received a copy of the GNU General Public License      *
 *  along with nymea. If not, see <http://www.gnu.org/licenses/>.          *
 *                                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef UPDATECONTROLLERPACKAGEKIT_H
#define UPDATECONTROLLERPACKAGEKIT_H

#include <QObject>
#include <QProcess>
#include <QNetworkAccessManager>
#include <Transaction>

#include "platform/platformupdatecontroller.h"

class UpdateControllerPackageKit: public PlatformUpdateController
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "io.nymea.PlatformUpdateController")
    Q_INTERFACES(PlatformUpdateController)

public:
    explicit UpdateControllerPackageKit(QObject *parent = nullptr);

    bool updateManagementAvailable() override;

    bool updateRunning() const override;

    QList<Package> packages() const override;
    virtual QList<Repository> repositories() const;

    bool startUpdate(const QStringList &packageIds = QStringList()) override;
    bool removePackages(const QStringList &packageIds) override;

    bool enableRepository(const QString &repositoryId, bool enabled) override;


private slots:
    void checkForUpdates();

private:
    void trackTransaction(PackageKit::Transaction* transaction);

private:
    QHash<QString, Package> m_packages;
    QHash<QString, Repository> m_repositories;

    QList<PackageKit::Transaction*> m_runningTransactions;
};

#endif // UPDATECONTROLLERPACKAGEKIT_H
