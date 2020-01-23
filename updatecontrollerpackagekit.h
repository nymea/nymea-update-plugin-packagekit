/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2020, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef UPDATECONTROLLERPACKAGEKIT_H
#define UPDATECONTROLLERPACKAGEKIT_H

#include <QObject>
#include <QProcess>
#include <QNetworkAccessManager>
#include <Transaction>
#include <QTimer>

#include "platform/platformupdatecontroller.h"

class UpdateControllerPackageKit: public PlatformUpdateController
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "io.nymea.PlatformUpdateController")
    Q_INTERFACES(PlatformUpdateController)

public:
    explicit UpdateControllerPackageKit(QObject *parent = nullptr);

    bool updateManagementAvailable() const override;

    bool checkForUpdates() override;

    bool busy() const override;
    bool updateRunning() const override;

    QList<Package> packages() const override;
    QList<Repository> repositories() const override;

    bool startUpdate(const QStringList &packageIds = QStringList()) override;
    bool removePackages(const QStringList &packageIds) override;

    bool enableRepository(const QString &repositoryId, bool enabled) override;

private slots:
    void refreshFromPackageKit();

private:
    void trackTransaction(PackageKit::Transaction* transaction);
    void trackUpdateTransaction(PackageKit::Transaction* transaction);

    void readDistro();
    bool addRepoManually(const QString &repo);

private:
    bool m_available = false;
    QHash<QString, Package> m_packages;
    QHash<QString, Repository> m_repositories;

    // Used to set the busy flag
    QList<PackageKit::Transaction*> m_runningTransactions;
    // Used to set the updateRunning flag
    QList<PackageKit::Transaction*> m_updateTransactions;

    // libpackagekitqt5 < 1.0 has a bug and emits the finished singal twice on getPackages.
    // We need to make sure we only handle it once. Could probably go away when everyone is upgraded
    // to libpackagekitqt5 >= 1.0.
    QList<PackageKit::Transaction*> m_unfinishedTransactions;

    QTimer *m_refreshTimer = nullptr;

    QString m_distro;
    QString m_component;
};

#endif // UPDATECONTROLLERPACKAGEKIT_H
