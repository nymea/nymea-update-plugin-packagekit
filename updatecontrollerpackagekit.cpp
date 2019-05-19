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

#include "updatecontrollerpackagekit.h"
#include "loggingcategories.h"

#include <Daemon>
#include <Details>

#include <QProcess>
#include <QTimer>

UpdateControllerPackageKit::UpdateControllerPackageKit(QObject *parent):
    PlatformUpdateController(parent)
{
    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::isRunningChanged, this, [this](){
        qCDebug(dcPlatformUpdate) << "Connected to PackageKit";
        if (PackageKit::Daemon::isRunning()) {
            PackageKit::Daemon::setHints("interactive=false");
            checkForUpdates();
        }
    });

    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::updatesChanged, this, [this]() {
        qCDebug(dcPlatformUpdate) << "Packagekit updatesChanged notification received";
        checkForUpdates();
    });
    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::daemonQuit, this, [this](){
        emit availableChanged();
    });
    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::changed, this, [](){
        qCDebug(dcPlatformUpdate) << "PackageKit changed notification received";
    });
}

bool UpdateControllerPackageKit::updateManagementAvailable()
{
    return PackageKit::Daemon::isRunning();
}

bool UpdateControllerPackageKit::updateRunning() const
{
    return m_runningTransactions.count() > 0;
}

QList<Package> UpdateControllerPackageKit::packages() const
{
    return m_packages.values();
}

QList<Repository> UpdateControllerPackageKit::repositories() const
{
    return m_repositories.values();
}

bool UpdateControllerPackageKit::startUpdate(const QStringList &packageIds)
{
    QStringList *upgradeIds = new QStringList();

    PackageKit::Transaction *getPackages = nullptr;
    if (packageIds.isEmpty()) {
        getPackages = PackageKit::Daemon::getUpdates();
    } else {
        getPackages = PackageKit::Daemon::getPackages(PackageKit::Transaction::FilterArch | PackageKit::Transaction::FilterNewest);
    }
    trackTransaction(getPackages);

    connect(getPackages, &PackageKit::Transaction::package, this, [upgradeIds, packageIds](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
        if (packageIds.isEmpty() || packageIds.contains(PackageKit::Daemon::packageName(packageID))) {
            upgradeIds->append(packageID);
        }
    });
    connect(getPackages, &PackageKit::Transaction::finished, this, [this, upgradeIds](){
        qCDebug(dcPlatform) << "List of packages to be upgraded:\n" << upgradeIds->join('\n');

        PackageKit::Transaction *upgrade = PackageKit::Daemon::updatePackages(*upgradeIds);
        trackTransaction(upgrade);
        delete upgradeIds;
        connect(upgrade, &PackageKit::Transaction::errorCode, this, [](PackageKit::Transaction::Error error, const QString &details){
            qCDebug(dcPlatformUpdate) << "Upgrade error:" << details << error;
        });
        connect(upgrade, &PackageKit::Transaction::package, this, [](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
            qCDebug(dcPlatformUpdate) << "Upgrading package:" << packageID << info << summary;
        });
        connect(upgrade, &PackageKit::Transaction::finished, this, [](){
            qCDebug(dcPlatformUpdate) << "Upgrade finished";
        });
    });
}

bool UpdateControllerPackageKit::removePackages(const QStringList &packageIds)
{
    qCDebug(dcPlatformUpdate) << "Starting removal of packages:" << packageIds;
    QStringList *removeIds = new QStringList();
    PackageKit::Transaction *getPackages = PackageKit::Daemon::getPackages(PackageKit::Transaction::FilterInstalled);
    trackTransaction(getPackages);
    connect(getPackages, &PackageKit::Transaction::package, this, [packageIds, removeIds](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
        if (packageIds.contains(PackageKit::Daemon::packageName(packageID))) {
            removeIds->append(packageID);
        }
    });
    connect(getPackages, &PackageKit::Transaction::finished, this, [this, removeIds](){
        qCDebug(dcPlatform) << "List of packages to be removed:\n" << removeIds->join('\n');

        PackageKit::Transaction *upgrade = PackageKit::Daemon::removePackages(*removeIds);
        trackTransaction(upgrade);
        delete removeIds;

        connect(upgrade, &PackageKit::Transaction::errorCode, this, [](PackageKit::Transaction::Error error, const QString &details){
            qCDebug(dcPlatformUpdate) << "Remove error:" << details << error;
        });
        connect(upgrade, &PackageKit::Transaction::package, this, [](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
            qCDebug(dcPlatformUpdate) << "Removing package:" << packageID << info << summary;
        });
        connect(upgrade, &PackageKit::Transaction::finished, this, [](){
            qCDebug(dcPlatformUpdate) << "Remove packages finished";
        });
    });
}

bool UpdateControllerPackageKit::enableRepository(const QString &repositoryId, bool enabled)
{
    qCDebug(dcPlatformUpdate) << "Enabling repo:" << repositoryId << enabled;
    PackageKit::Transaction *repoTransaction = PackageKit::Daemon::repoEnable(repositoryId, enabled);
    trackTransaction(repoTransaction);
    connect(repoTransaction, &PackageKit::Transaction::finished, this, [repositoryId, enabled](){
        qCDebug(dcPlatformUpdate) << "Repository" << repositoryId << (enabled ? "enabled" : "disabled");
    });
    connect(repoTransaction, &PackageKit::Transaction::errorCode, this, [repositoryId, enabled](PackageKit::Transaction::Error error, const QString &details){
        qCDebug(dcPlatformUpdate) << "Error" << (enabled ? "enabling" : "disabling") << "repository" << repositoryId;
    });
}

void UpdateControllerPackageKit::checkForUpdates()
{
    if (m_runningTransactions.count() > 0) {
        return;
    }
    qCDebug(dcPlatformUpdate) << "Start update procedure...";

    QHash<QString, Package> *newPackageList = new QHash<QString, Package>();

    qCDebug(dcPlatformUpdate) << "Fetching installed packages...";
    PackageKit::Transaction *getInstalled = PackageKit::Daemon::getPackages();
    trackTransaction(getInstalled);
    connect(getInstalled, &PackageKit::Transaction::package, this, [this, newPackageList](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary) {
        if (PackageKit::Daemon::packageName(packageID).contains("nymea")) {
//            qCDebug(dcPlatformUpdate) << "Have installed package:" << PackageKit::Daemon::packageName(packageID) << PackageKit::Daemon::packageVersion(packageID);

            // Note: We're using packageName as ID because packageId is different for different versions of a package.
            // However, in nymea we handle things differently, a package ID should identify a package without version info
            // Given that packagekit backends (e.g. apt, rpm) also handle package installs via name, that should be just fine.
            QString packageName = PackageKit::Daemon::packageName(packageID);
            if (newPackageList->contains(packageName)) {
                if (info == PackageKit::Transaction::InfoInstalled) {
                    (*newPackageList)[packageName].setInstalledVersion(PackageKit::Daemon::packageVersion(packageID));
                    (*newPackageList)[packageName].setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
                    (*newPackageList)[packageName].setCanRemove(true);
                }
            } else {
                Package package(packageName, packageName);
                if (info == PackageKit::Transaction::InfoInstalled) {
                    package.setInstalledVersion(PackageKit::Daemon::packageVersion(packageID));
                    package.setCanRemove(true);
                }
                package.setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
                newPackageList->insert(packageName, package);
            }
        }
    });
    connect(getInstalled, &PackageKit::Transaction::finished, this, [this, newPackageList](){

        qCDebug(dcPlatformUpdate) << "Fetching installed packages finished. Fetching updates...";

        PackageKit::Transaction *getUpdates = PackageKit::Daemon::getUpdates();
        trackTransaction(getUpdates);
        connect(getUpdates, &PackageKit::Transaction::package, this, [this, newPackageList](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
            if (PackageKit::Daemon::packageName(packageID).contains("nymea")) {
                qCDebug(dcPlatformUpdate) << "Update available for package:" << PackageKit::Daemon::packageName(packageID) << PackageKit::Daemon::packageVersion(packageID);
                QString packageName = PackageKit::Daemon::packageName(packageID);
                (*newPackageList)[packageName].setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
                (*newPackageList)[packageName].setUpdateAvailable(true);
            }
        });
        connect(getUpdates, &PackageKit::Transaction::finished, this, [this, newPackageList](){
            qCDebug(dcPlatformUpdate) << "Fetching updates finished.";

            QStringList packagesToRemove;
            foreach (const QString &id, m_packages.keys()) {
                if (!newPackageList->contains(id)) {
                    packagesToRemove.append(id);
                }
            }
            while (!packagesToRemove.isEmpty()) {
                Package p = m_packages.take(packagesToRemove.takeFirst());
                qCDebug(dcPlatformUpdate) << "Removed package" << p.packageId();
                emit packageRemoved(p.packageId());
            }

            foreach (const QString &id, newPackageList->keys()) {
                if (!m_packages.contains(id)) {
                    m_packages.insert(id, newPackageList->value(id));
                    qCDebug(dcPlatformUpdate) << "Added package" << id;
                    emit packageAdded(newPackageList->value(id));
                } else {
                    if (m_packages.value(id) != newPackageList->value(id)) {
                        m_packages[id] = newPackageList->value(id);
                        qCDebug(dcPlatformUpdate) << "Package" << id << "changed";
                        emit packageChanged(m_packages[id]);
                    }
                }
            }
            delete newPackageList;
        });


//        PackageKit::Transaction *t = PackageKit::Daemon::getPackages(PackageKit::Transaction::FilterNotInstalled | PackageKit::Transaction::FilterNotDevel);
//        connect(t, &PackageKit::Transaction::package, this, [this, t](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary) {
//            if (PackageKit::Daemon::packageName(packageID).contains("nymea")) {
//                QString packageName = PackageKit::Daemon::packageName(packageID);
//                qCDebug(dcPlatformUpdate) << "Have package:" << packageName << PackageKit::Daemon::packageVersion(packageID);
//                if (m_packages.contains(packageName)) {
//                    m_packages[packageName].setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
//                    emit packageChanged(m_packages[packageName]);
//                } else {
//                    Package package(packageName, packageName);
//                    package.setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
//                    m_packages.insert(packageName, package);
//                    emit packageAdded(package);
//                }
//            }
//        });
//        connect(t, &PackageKit::Transaction::finished, this, [this, t](){
//            qCDebug(dcPlatformUpdate) << "finished";

//        });

    });


    PackageKit::Transaction *getRepos = PackageKit::Daemon::getRepoList();
    trackTransaction(getRepos);
    connect(getRepos, &PackageKit::Transaction::repoDetail, this, [this](const QString &repoId, const QString &description, bool enabled){
        if (repoId.contains("ci-repo.nymea.io/") && !repoId.contains("deb-src")) {
            qCDebug(dcPlatformUpdate) << "Have Repo:" << repoId << description << enabled;
            if (m_repositories.contains(repoId)) {
                m_repositories[repoId].setEnabled(enabled);
                emit repositoryChanged(m_repositories.value(repoId));
            } else {
                QString description = repoId;
                if (repoId.contains("experimental-silo")) {
                    description = "Experimental";
                } else if (repoId.contains("landing-silo")) {
                    description = "Testing";
                }
                Repository repo(repoId, description, enabled);
                m_repositories.insert(repoId, repo);
                emit repositoryAdded(repo);
            }
        }
    });
}

void UpdateControllerPackageKit::trackTransaction(PackageKit::Transaction *transaction)
{
    m_runningTransactions.append(transaction);
    if (m_runningTransactions.count() == 1) {
        emit updateRunningChanged();
    }
    connect(transaction, &PackageKit::Transaction::finished, this, [this, transaction](){
        m_runningTransactions.removeAll(transaction);
        if (m_runningTransactions.count() == 0) {
            emit updateRunningChanged();
        }
    });
}
