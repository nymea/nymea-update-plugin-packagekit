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

#include "updatecontrollerpackagekit.h"
#include "loggingcategories.h"

#include <Daemon>
#include <Details>

#include <QTimer>
#include <QPointer>
#include <QFile>

UpdateControllerPackageKit::UpdateControllerPackageKit(QObject *parent):
    PlatformUpdateController(parent)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(6 * 60 * 60 * 1000); // every 6 hours
    connect(m_refreshTimer, &QTimer::timeout, this, &UpdateControllerPackageKit::checkForUpdates);

    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::isRunningChanged, this, [this](){
        if (PackageKit::Daemon::isRunning()) {
            qCDebug(dcPlatformUpdate) << "Connected to PackageKit";
            PackageKit::Daemon::setHints("interactive=false");

            m_available = true;
            emit availableChanged();

            refreshFromPackageKit();

        } else {
            qCWarning(dcPlatformUpdate()) << "Connection to PackageKit lost";
            // No worries, it'll be autostarted via dbus
        }
    });

    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::updatesChanged, this, [this]() {
        qCDebug(dcPlatformUpdate) << "Packagekit updatesChanged notification received";
        refreshFromPackageKit();
    });
    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::changed, this, [this](){
        qCDebug(dcPlatformUpdate) << "PackageKit ready" << PackageKit::Daemon::distroID();
        readDistro();
        checkForUpdates();
    });
}

bool UpdateControllerPackageKit::updateManagementAvailable() const
{
    return m_available;
}

bool UpdateControllerPackageKit::checkForUpdates()
{
    qCDebug(dcPlatformUpdate()) << "Refreshing system package cache...";
    PackageKit::Transaction *refreshCache = PackageKit::Daemon::refreshCache(true);
    connect(refreshCache, &PackageKit::Transaction::finished, this, [this](){
        qCDebug(dcPlatformUpdate()) << "System package cache refreshed. Next update is at" << QDateTime::currentDateTime().addMSecs(m_refreshTimer->interval());
        m_refreshTimer->start();
        refreshFromPackageKit();
    });
    trackTransaction(refreshCache);
    return true;
}

bool UpdateControllerPackageKit::busy() const
{
    return m_runningTransactions.count() > 0 || m_updateTransactions.count() > 0;
}

bool UpdateControllerPackageKit::updateRunning() const
{
    return m_updateTransactions.count() > 0;
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
    qCDebug(dcPlatformUpdate) << "Starting to update" << packageIds;
    QHash<QString, QString> *upgradeIds = new QHash<QString, QString>; // <packageName, packageId>

    // First, fetch packages with those ids. Installed and not installed ones. if packageIds is empty, this will be a no-op
    PackageKit::Transaction *getPackages = PackageKit::Daemon::getPackages(PackageKit::Transaction::FilterArch);
    m_unfinishedTransactions.append(getPackages);

    connect(getPackages, &PackageKit::Transaction::package, this, [upgradeIds, packageIds](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
        Q_UNUSED(info)
        Q_UNUSED(summary)
        if (packageIds.contains(PackageKit::Daemon::packageName(packageID))) {
            qCDebug(dcPlatformUpdate) << "Adding package to be installed:" << packageID;
            upgradeIds->insert(PackageKit::Daemon::packageName(packageID), packageID);
        }
    });
    connect(getPackages, &PackageKit::Transaction::finished, this, [this, packageIds, upgradeIds, getPackages](){

        if (!m_unfinishedTransactions.contains(getPackages)) {
            qCWarning(dcPlatformUpdate) << "Transaction emitted finished twice! Ignoring second event. (Old packagekitqt version?)";
            return;
        }
        m_unfinishedTransactions.removeAll(getPackages);

        // OK, we've got packages for all the packageIds. Now get potential updates.
        PackageKit::Transaction *getUpdates = PackageKit::Daemon::getUpdates();
        m_unfinishedTransactions.append(getUpdates);
        connect(getUpdates, &PackageKit::Transaction::package, this, [packageIds, upgradeIds](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
            Q_UNUSED(info)
            Q_UNUSED(summary)
            if (packageIds.isEmpty() || packageIds.contains(PackageKit::Daemon::packageName(packageID))) {
                qCDebug(dcPlatformUpdate) << "Adding package to be updated:" << packageID;
                upgradeIds->insert(PackageKit::Daemon::packageName(packageID), packageID);
            }
        });
        connect(getUpdates, &PackageKit::Transaction::finished, this, [this, upgradeIds, getUpdates](){
            if (!m_unfinishedTransactions.contains(getUpdates)) {
                qCWarning(dcPlatformUpdate) << "Transaction emitted finished twice! Ignoring second event. (Old packagekitqt version?)";
                return;
            }
            m_unfinishedTransactions.removeAll(getUpdates);

            qCDebug(dcPlatform) << "List of packages to be upgraded:\n" << upgradeIds->values().join('\n');

            PackageKit::Transaction *upgrade = PackageKit::Daemon::updatePackages(upgradeIds->values());
            delete upgradeIds;
            connect(upgrade, &PackageKit::Transaction::errorCode, this, [this](PackageKit::Transaction::Error error, const QString &details){
                qCDebug(dcPlatformUpdate) << "Upgrade error:" << error << details;
                if (error == PackageKit::Transaction::ErrorPackageDownloadFailed) {
                    // Download failed... looks like the server doesn't host the .deb files (any more). Let's refresh the cache.
                    checkForUpdates();
                }
            });
            connect(upgrade, &PackageKit::Transaction::package, this, [this](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
                qCDebug(dcPlatformUpdate) << "Upgrading package:" << packageID << info << summary;
                if (info == PackageKit::Transaction::InfoFinished) {
                    QString id = PackageKit::Daemon::packageName(packageID);
                    m_packages[id].setInstalledVersion(PackageKit::Daemon::packageVersion(packageID));
                    m_packages[id].setCandidateVersion(QString());
                    m_packages[id].setUpdateAvailable(false);
                    emit packageChanged(m_packages[id]);
                }
            });
            connect(upgrade, &PackageKit::Transaction::finished, this, [](){
                qCDebug(dcPlatformUpdate) << "Upgrade finished";
            });
            trackUpdateTransaction(upgrade);

        });
        trackUpdateTransaction(getUpdates);

    });
    trackUpdateTransaction(getPackages);
    return true;
}

bool UpdateControllerPackageKit::removePackages(const QStringList &packageIds)
{
    qCDebug(dcPlatformUpdate) << "Starting removal of packages:" << packageIds;
    QStringList *removeIds = new QStringList();
    PackageKit::Transaction *getPackages = PackageKit::Daemon::getPackages(PackageKit::Transaction::FilterInstalled);
    m_unfinishedTransactions.append(getPackages);

    connect(getPackages, &PackageKit::Transaction::package, this, [packageIds, removeIds](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
        Q_UNUSED(info)
        Q_UNUSED(summary)
        if (packageIds.contains(PackageKit::Daemon::packageName(packageID))) {
            removeIds->append(packageID);
        }
    });
    connect(getPackages, &PackageKit::Transaction::finished, this, [this, removeIds, getPackages](){

        if (!m_unfinishedTransactions.contains(getPackages)) {
            qCWarning(dcPlatformUpdate) << "Transaction emitted finished twice! Ignoring second event. (Old packagekitqt version?)";
            return;
        }
        m_unfinishedTransactions.removeAll(getPackages);

        qCDebug(dcPlatformUpdate) << "List of packages to be removed:\n" << removeIds->join('\n');

        PackageKit::Transaction *remove = PackageKit::Daemon::removePackages(*removeIds);
        delete removeIds;

        connect(remove, &PackageKit::Transaction::errorCode, this, [](PackageKit::Transaction::Error error, const QString &details){
            qCDebug(dcPlatformUpdate) << "Remove error:" << details << error;
        });
        connect(remove, &PackageKit::Transaction::package, this, [this](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
            qCDebug(dcPlatformUpdate) << "Removing package:" << packageID << info << summary;
            if (info == PackageKit::Transaction::InfoFinished) {
                QString id = PackageKit::Daemon::packageName(packageID);
                m_packages[id].setInstalledVersion(QString());
                m_packages[id].setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
                m_packages[id].setCanRemove(true);
                emit packageChanged(m_packages[id]);
            }
        });
        connect(remove, &PackageKit::Transaction::finished, this, [](){
            qCDebug(dcPlatformUpdate) << "Remove packages finished";
        });

        trackUpdateTransaction(remove);
    });

    trackUpdateTransaction(getPackages);
    return true;
}

bool UpdateControllerPackageKit::enableRepository(const QString &repositoryId, bool enabled)
{
    if (repositoryId.startsWith("virtual_")) {
        bool success = addRepoManually(repositoryId);
        if (success) {
            m_repositories[repositoryId].setEnabled(enabled);
            emit repositoryChanged(m_repositories.value(repositoryId));
        }
        return success;
    }

    qCDebug(dcPlatformUpdate) << "Enabling repo:" << repositoryId << enabled;
    PackageKit::Transaction *repoTransaction = PackageKit::Daemon::repoEnable(repositoryId, enabled);
    connect(repoTransaction, &PackageKit::Transaction::finished, this, [repositoryId, enabled](){
        qCDebug(dcPlatformUpdate) << "Repository" << repositoryId << (enabled ? "enabled" : "disabled");
    });
    connect(repoTransaction, &PackageKit::Transaction::errorCode, this, [repositoryId, enabled](PackageKit::Transaction::Error error, const QString &details){
        qCDebug(dcPlatformUpdate) << "Error" << (enabled ? "enabling" : "disabling") << "repository" << repositoryId << "(" << error << details << ")";
    });
    trackTransaction(repoTransaction);

    m_repositories[repositoryId].setEnabled(enabled);
    emit repositoryChanged(m_repositories.value(repositoryId));

    checkForUpdates();

    return true;
}

void UpdateControllerPackageKit::refreshFromPackageKit()
{
    if (m_runningTransactions.count() > 0) {
        return;
    }
    QHash<QString, Package>* newPackageList = new QHash<QString, Package>();

    qCDebug(dcPlatformUpdate) << "Reading installed/available packages from backend...";
    PackageKit::Transaction *getInstalled = PackageKit::Daemon::getPackages(PackageKit::Transaction::FilterNotDevel);

    m_unfinishedTransactions.append(getInstalled);

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
                package.setSummary(summary);
                if (info == PackageKit::Transaction::InfoInstalled) {
                    package.setInstalledVersion(PackageKit::Daemon::packageVersion(packageID));
                    package.setCanRemove(true);
                }
                package.setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
                newPackageList->insert(packageName, package);
            }
        }
    });
    connect(getInstalled, &PackageKit::Transaction::finished, this, [this, newPackageList, getInstalled](){

        if (!m_unfinishedTransactions.contains(getInstalled)) {
            qCWarning(dcPlatformUpdate) << "Transaction emitted finished twice! Ignoring second event. (Old packagekitqt version?)";
            return;
        }
        m_unfinishedTransactions.removeAll(getInstalled);

        qCDebug(dcPlatformUpdate) << "Fetching installed/available packages finished. Fetching list of possible updates from backend...";

        PackageKit::Transaction *getUpdates = PackageKit::Daemon::getUpdates();
        m_unfinishedTransactions.append(getUpdates);
        connect(getUpdates, &PackageKit::Transaction::package, this, [this, newPackageList](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary){
            Q_UNUSED(info)
            if (PackageKit::Daemon::packageName(packageID).contains("nymea")) {
                qCDebug(dcPlatformUpdate) << "Update available for package:" << PackageKit::Daemon::packageName(packageID) << PackageKit::Daemon::packageVersion(packageID);
                QString packageName = PackageKit::Daemon::packageName(packageID);
                if (!newPackageList->contains(packageName)) { // Might happen for -dev and -dbg packages as we filter them in the previous call
                    (*newPackageList)[packageName] = Package(packageName, packageName);
                }
                (*newPackageList)[packageName].setSummary(summary);
                (*newPackageList)[packageName].setCandidateVersion(PackageKit::Daemon::packageVersion(packageID));
                (*newPackageList)[packageName].setUpdateAvailable(true);
            }
        });
        connect(getUpdates, &PackageKit::Transaction::finished, this, [this, newPackageList, getUpdates](){

            if (!m_unfinishedTransactions.contains(getUpdates)) {
                qCWarning(dcPlatformUpdate) << "Transaction emitted finished twice! Ignoring second event. (Old packagekitqt version?)";
                return;
            }
            m_unfinishedTransactions.removeAll(getUpdates);

            qCDebug(dcPlatformUpdate) << "Fetching possible updates finished.";
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
        trackTransaction(getUpdates);
    });
    trackTransaction(getInstalled);


    qCDebug(dcPlatformUpdate()) << "Fetching list of repositories from backend...";
    PackageKit::Transaction *getRepos = PackageKit::Daemon::getRepoList(PackageKit::Transaction::FilterNotSource);
    connect(getRepos, &PackageKit::Transaction::repoDetail, this, [this](const QString &repoId, const QString &description, bool enabled){
        if (repoId.contains("ci-repo.nymea.io/") && !repoId.contains("deb-src")) {
            qCDebug(dcPlatformUpdate) << "Found repository enabled in system:" << repoId << description << (enabled ? "(enabled)" : "(disabled)");
            if (m_repositories.contains(repoId)) {
                m_repositories[repoId].setEnabled(enabled);
                qCDebug(dcPlatformUpdate) << "Updating existing repository in state cache:" << repoId << (enabled ? "(enabled)" : "(disabled)");
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
                qCDebug(dcPlatformUpdate) << "Adding new repository to state cache:" << repoId << description << (enabled ? "(enabled)" : "(disabled)");
                emit repositoryAdded(repo);
            }
        }
    });
    connect(getRepos, &PackageKit::Transaction::finished, this, [this](){
        if (m_distro.isEmpty()) {
            qCWarning(dcPlatformUpdate) << "Running on an unknowm distro. Not adding testing/experimental repository";
            return;
        }
        bool foundTesting = false;
        bool foundExperimental = false;
        foreach (const QString &repoId, m_repositories.keys()) {
            if (repoId.contains("ci-repo.nymea.io/landing-silo")) {
                if (m_repositories.contains("virtual_testing")) {
                    qCDebug(dcPlatformUpdate) << "Replacing virtual_testing with real landing-silo";
                    m_repositories.remove("virtual_testing");
                    emit repositoryRemoved("virtual_testing");
                }
                foundTesting = true;
                continue;
            }
            if (repoId.contains("ci-repo.nymea.io/experimental-silo")) {
                if (m_repositories.contains("virtual_experimental")) {
                    qCDebug(dcPlatformUpdate) << "Replacing virtual_experimental with real experimental-silo";
                    m_repositories.remove("virtual_experimental");
                    emit repositoryRemoved("virtual_experimental");
                }
                foundExperimental = true;
                continue;
            }
        }

        if (!foundTesting && !m_repositories.contains("virtual_testing")) {
            QString id = "virtual_testing";
            Repository repository(id, "Testing", false);
            m_repositories.insert(id, repository);
            qCDebug(dcPlatformUpdate) << "Testing not found. Adding virtual repo:" << id;
            emit repositoryAdded(repository);
        }
        if (!foundExperimental && !m_repositories.contains("virtual_experimental")) {
            QString id = "virtual_experimental";
            Repository repository(id, "Experimental", false);
            m_repositories.insert(id, repository);
            qCDebug(dcPlatformUpdate) << "Experimental not found. Adding virtual repo:" << id;
            emit repositoryAdded(repository);
        }
    });
    trackTransaction(getRepos);

}

void UpdateControllerPackageKit::trackTransaction(PackageKit::Transaction *transaction)
{
    m_runningTransactions.append(transaction);
    qCDebug(dcPlatformUpdate) << "Started transaction" << transaction << "(" << m_runningTransactions.count() << "running)";
    if (m_runningTransactions.count() > 0) {
        emit busyChanged();
    }
    connect(transaction, &PackageKit::Transaction::finished, this, [this, transaction](){
        m_runningTransactions.removeAll(transaction);
        qCDebug(dcPlatformUpdate) << "Transaction" << transaction << " finished (" << m_runningTransactions.count() << "running)";
        if (m_runningTransactions.count() == 0) {
            emit busyChanged();
        }
    });
}

void UpdateControllerPackageKit::trackUpdateTransaction(PackageKit::Transaction *transaction)
{
    m_updateTransactions.append(transaction);
    qCDebug(dcPlatformUpdate) << "Started update transaction" << transaction << "(" << m_updateTransactions.count() << "running)";
    if (m_updateTransactions.count() == 1) {
        emit updateRunningChanged();
    }
    connect(transaction, &PackageKit::Transaction::finished, this, [this, transaction](){
        m_updateTransactions.removeAll(transaction);
        qCDebug(dcPlatformUpdate) << "Update Transaction" << transaction << "finished (" << m_updateTransactions.count() << "running)";
        if (m_updateTransactions.count() == 0) {
            emit updateRunningChanged();
        }
    });
}

void UpdateControllerPackageKit::readDistro()
{
    if (!PackageKit::Daemon::mimeTypes().contains("application/x-deb")) {
        qCWarning(dcPlatformUpdate()) << "Not running on a dpkg based distro. Update features won't be available.";
        return;
    }
    QHash<QString, QString> knownDistros;
    // Ubuntu
    knownDistros.insert("16.04", "xenial");
    knownDistros.insert("18.04", "bionic");
    knownDistros.insert("19.04", "disco");
    knownDistros.insert("19.10", "eoan");
    knownDistros.insert("20.04", "focal");
    knownDistros.insert("20.10", "groovy");
    knownDistros.insert("21.04", "hirsute");
    // Debian
    knownDistros.insert("9", "stretch");
    knownDistros.insert("10", "buster");

    QStringList distroInfo = PackageKit::Daemon::distroID().split(';');
    qCDebug(dcPlatformUpdate()) << "Running on distro:" << distroInfo;
    if (distroInfo.count() != 3) {
        qCWarning(dcPlatformUpdate()) << "Cannot read distro info" << PackageKit::Daemon::distroID();
        return;
    }
    QString distroVersion = QString(distroInfo.at(1)).remove("\"");
    if (!knownDistros.contains(distroVersion)) {
        qCWarning(dcPlatformUpdate()) << "Distro" << PackageKit::Daemon::distroID() << "is unknown.";
        return;
    }

    QString distroCodename = QString(distroInfo.first());
    if (distroCodename == "raspbian") {
        m_component = "rpi";
    } else {
        m_component = "main";
    }

    m_distro = knownDistros.value(distroVersion);
}

bool UpdateControllerPackageKit::addRepoManually(const QString &repo)
{
    if (m_distro.isEmpty()) {
        qCWarning(dcPlatformUpdate()) << "Error reading distro info. Cannot add repository" << repo;
        return false;
    }
    QHash<QString, QString> repos;
    repos.insert("virtual_testing", "deb http://ci-repo.nymea.io/landing-silo " + m_distro + " " + m_component);
    repos.insert("virtual_experimental", "deb http://ci-repo.nymea.io/experimental-silo " + m_distro + " " + m_component);

    if (!repos.contains(repo)) {
        qCWarning(dcPlatformUpdate()) << "Cannot add unknown repo" << repo;
        return false;
    }

    QString fileName("/etc/apt/sources.list.d/nymea.list");
    QFile sourcesList(fileName);
    if (!sourcesList.open(QFile::ReadWrite)) {
        qCWarning(dcPlatformUpdate()) << "Failed to open" << fileName << "for writing. Not adding repo.";
        return false;
    }
    bool ok;
    ok = sourcesList.seek(sourcesList.size());
    QString line = QString("\n\n%1\n").arg(repos.value(repo));
    qint64 ret = sourcesList.write(line.toUtf8());
    ok &= (ret == line.length());
    if (!ok) {
        qCWarning(dcPlatformUpdate()) << "Failed to write repository to file" << fileName;
        return false;
    }
    qCDebug(dcPlatform()) << "Added repository" << repos.value(repo);

    checkForUpdates();

    return true;
}
