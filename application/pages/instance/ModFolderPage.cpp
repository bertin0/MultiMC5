/* Copyright 2013-2019 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ModFolderPage.h"
#include "ui_ModFolderPage.h"

#include <QMessageBox>
#include <QEvent>
#include <QKeyEvent>
#include <QAbstractItemModel>

#include "MultiMC.h"
#include "dialogs/CustomMessageBox.h"
#include "dialogs/ModEditDialogCommon.h"
#include <GuiUtil.h>
#include "minecraft/SimpleModList.h"
#include "minecraft/Mod.h"
#include "minecraft/VersionFilterData.h"
#include "minecraft/ComponentList.h"
#include <DesktopServices.h>

ModFolderPage::ModFolderPage(BaseInstance *inst, std::shared_ptr<SimpleModList> mods, QString id,
                             QString iconName, QString displayName, QString helpPage,
                             QWidget *parent)
    : QWidget(parent), ui(new Ui::ModFolderPage)
{
    ui->setupUi(this);
    ui->tabWidget->tabBar()->hide();
    m_inst = inst;
    on_RunningState_changed(m_inst && m_inst->isRunning());
    m_mods = mods;
    m_id = id;
    m_displayName = displayName;
    m_iconName = iconName;
    m_helpName = helpPage;
    m_fileSelectionFilter = "%1 (*.zip *.jar)";
    m_filterModel = new QSortFilterProxyModel(this);
    m_filterModel->setDynamicSortFilter(true);
    m_filterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filterModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_filterModel->setSourceModel(m_mods.get());
    m_filterModel->setFilterKeyColumn(-1);
    ui->modTreeView->setModel(m_filterModel);
    ui->modTreeView->installEventFilter(this);
    ui->modTreeView->sortByColumn(1, Qt::AscendingOrder);
    auto smodel = ui->modTreeView->selectionModel();
    connect(smodel, &QItemSelectionModel::currentChanged, this, &ModFolderPage::modCurrent);
    connect(ui->filterEdit, &QLineEdit::textChanged, this, &ModFolderPage::on_filterTextChanged );
    connect(m_inst, &BaseInstance::runningStatusChanged, this, &ModFolderPage::on_RunningState_changed);
}

void ModFolderPage::openedImpl()
{
    m_mods->startWatching();
}

void ModFolderPage::closedImpl()
{
    m_mods->stopWatching();
}

void ModFolderPage::on_filterTextChanged(const QString& newContents)
{
    m_viewFilter = newContents;
    m_filterModel->setFilterFixedString(m_viewFilter);
}


CoreModFolderPage::CoreModFolderPage(BaseInstance *inst, std::shared_ptr<SimpleModList> mods,
                                     QString id, QString iconName, QString displayName,
                                     QString helpPage, QWidget *parent)
    : ModFolderPage(inst, mods, id, iconName, displayName, helpPage, parent)
{
}

ModFolderPage::~ModFolderPage()
{
    m_mods->stopWatching();
    delete ui;
}

void ModFolderPage::on_RunningState_changed(bool running)
{
    if(m_controlsEnabled == !running) {
        return;
    }
    m_controlsEnabled = !running;
    ui->addModBtn->setEnabled(m_controlsEnabled);
    ui->disableModBtn->setEnabled(m_controlsEnabled);
    ui->enableModBtn->setEnabled(m_controlsEnabled);
    ui->rmModBtn->setEnabled(m_controlsEnabled);
}

bool ModFolderPage::shouldDisplay() const
{
    return true;
}

bool CoreModFolderPage::shouldDisplay() const
{
    if (ModFolderPage::shouldDisplay())
    {
        auto inst = dynamic_cast<MinecraftInstance *>(m_inst);
        if (!inst)
            return true;
        auto version = inst->getComponentList();
        if (!version)
            return true;
        if(!version->getComponent("net.minecraftforge"))
        {
            return false;
        }
        if(!version->getComponent("net.minecraft"))
        {
            return false;
        }
        if(version->getComponent("net.minecraft")->getReleaseDateTime() < g_VersionFilterData.legacyCutoffDate)
        {
            return true;
        }
    }
    return false;
}

bool ModFolderPage::modListFilter(QKeyEvent *keyEvent)
{
    switch (keyEvent->key())
    {
    case Qt::Key_Delete:
        on_rmModBtn_clicked();
        return true;
    case Qt::Key_Plus:
        on_addModBtn_clicked();
        return true;
    default:
        break;
    }
    return QWidget::eventFilter(ui->modTreeView, keyEvent);
}

bool ModFolderPage::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() != QEvent::KeyPress)
    {
        return QWidget::eventFilter(obj, ev);
    }
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
    if (obj == ui->modTreeView)
        return modListFilter(keyEvent);
    return QWidget::eventFilter(obj, ev);
}

void ModFolderPage::on_addModBtn_clicked()
{
    if(!m_controlsEnabled) {
        return;
    }
    auto list = GuiUtil::BrowseForFiles(
        m_helpName,
        tr("Select %1",
           "Select whatever type of files the page contains. Example: 'Loader Mods'")
            .arg(m_displayName),
        m_fileSelectionFilter.arg(m_displayName), MMC->settings()->get("CentralModsDir").toString(),
        this->parentWidget());
    if (!list.empty())
    {
        for (auto filename : list)
        {
            m_mods->installMod(filename);
        }
    }
}

void ModFolderPage::on_enableModBtn_clicked()
{
    if(!m_controlsEnabled) {
        return;
    }
    auto selection = m_filterModel->mapSelectionToSource(ui->modTreeView->selectionModel()->selection());
    m_mods->enableMods(selection.indexes(), true);
}

void ModFolderPage::on_disableModBtn_clicked()
{
    if(!m_controlsEnabled) {
        return;
    }
    auto selection = m_filterModel->mapSelectionToSource(ui->modTreeView->selectionModel()->selection());
    m_mods->enableMods(selection.indexes(), false);
}

void ModFolderPage::on_rmModBtn_clicked()
{
    if(!m_controlsEnabled) {
        return;
    }
    auto selection = m_filterModel->mapSelectionToSource(ui->modTreeView->selectionModel()->selection());
    m_mods->deleteMods(selection.indexes());
}

void ModFolderPage::on_configFolderBtn_clicked()
{
    DesktopServices::openDirectory(m_inst->instanceConfigFolder(), true);
}

void ModFolderPage::on_viewModBtn_clicked()
{
    DesktopServices::openDirectory(m_mods->dir().absolutePath(), true);
}

void ModFolderPage::modCurrent(const QModelIndex &current, const QModelIndex &previous)
{
    if (!current.isValid())
    {
        ui->frame->clear();
        return;
    }
    auto sourceCurrent = m_filterModel->mapToSource(current);
    int row = sourceCurrent.row();
    Mod &m = m_mods->operator[](row);
    ui->frame->updateWithMod(m);
}
