/***************************************************************************
 *   Copyright (C) 2003 by Sébastien Laoût                                 *
 *   slaout@linux62.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "mainwindow.h"

#include <QMoveEvent>
#include <QResizeEvent>
#include <QToolBar>

#include <KDE/KAction>
#include <KDE/KApplication>
#include <QLocale>
#include <KDE/KEditToolBar>
#include <QStatusBar>
#include <KDE/KDebug>
#include <QMessageBox>
#include <KDE/KConfig>
#include <KDE/KAboutData>
#include <KDE/KActionCollection>
#include <KDE/KToggleAction>

#include <KDE/KSettings/Dialog>

#include <kdeversion.h>

#include <QtGui/QDesktopWidget>

#include "settings.h"
#include "global.h"
#include "bnpview.h"
#include "basketstatusbar.h"

/** Container */

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent), m_settings(0), m_quit(false)
{
    BasketStatusBar* bar = new BasketStatusBar(statusBar());
    m_mainToolBar = addToolBar("Main");
    m_editToolBar = addToolBar("Edit");
    m_baskets = new BNPView(this, "BNPViewApp", this, bar, m_mainToolBar, m_editToolBar);
    setCentralWidget(m_baskets);

    setupActions();
    statusBar()->show();
    statusBar()->setSizeGripEnabled(true);

//    setAutoSaveSettings(/*groupName=*/QString::fromLatin1("MainWindow"), /*saveWindowSize=*//*FIXME:false:Why was it false??*/true);

//    m_actShowToolbar->setChecked(m_mainToolBar->isVisible());
//    m_actShowStatusbar->setChecked(statusBar()->isVisible());
    connect(m_baskets,      SIGNAL(setWindowCaption(const QString &)), this, SLOT(setCaption(const QString &)));

//  InlineEditors::instance()->richTextToolBar();
//    setStandardToolBarMenuEnabled(true);

//    KConfigGroup group = KGlobal::config()->group(autoSaveGroup());
//    applyMainWindowSettings(group);
}

MainWindow::~MainWindow()
{
//    KConfigGroup group = KGlobal::config()->group(autoSaveGroup());
//    saveMainWindowSettings(group);
    delete m_settings;
    delete m_baskets;
}

void MainWindow::setupActions()
{
    //////////////////////////////////////////////////////////////////////////
    /** GLOBAL SHORTCUTS : **************************************************/
    //////////////////////////////////////////////////////////////////////////
    QAction *a = NULL;

    // Ctrl+Shift+W only works when started standalone:
    QWidget *basketMainWindow =
        qobject_cast<QMainWindow *>(Global::bnpView->parent());

    int modifier = Qt::CTRL + Qt::ALT + Qt::SHIFT;

    if (basketMainWindow) {
        a = new QAction(this->parent());
        connect(a, SIGNAL(triggered()), Global::systemTray, SLOT(toggleActive()));
        a->setText(tr("Show/hide main window"));
        a->setStatusTip(
            tr("Allows you to show main Window if it is hidden, and to hide "
                 "it if it is shown."));
        a->setShortcut(QKeySequence(modifier + Qt::Key_W));
        a->setShortcutContext(Qt::ApplicationShortcut);
    }

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(globalPasteInCurrentBasket()));
    a->setText(tr("Paste clipboard contents in current basket"));
    a->setStatusTip(
        tr("Allows you to paste clipboard contents in the current basket "
             "without having to open the main window."));
    a->setShortcut(QKeySequence(modifier + Qt::Key_V));
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered(bool)), Global::bnpView, SLOT(showPassiveContent(bool)));
    a->setText(tr("Show current basket name"));
    a->setStatusTip(tr("Allows you to know basket is current without opening "
                         "the main window."));
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(pasteInCurrentBasket()));
    a->setText(tr("Paste selection in current basket"));
    a->setStatusTip(
        tr("Allows you to paste clipboard selection in the current basket "
             "without having to open the main window."));
    a->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_S));
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(askNewBasket()));
    a->setText(tr("Create a new basket"));
    a->setStatusTip(
        tr("Allows you to create a new basket without having to open the "
             "main window (you then can use the other global shortcuts to add "
             "a note, paste clipboard or paste selection in this new basket).")
    );
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(goToPreviousBasket()));
    a->setText(tr("Go to previous basket"));
    a->setStatusTip(
        tr("Allows you to change current basket to the previous one without "
             "having to open the main window."));
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(goToNextBasket()));
    a->setText(tr("Go to next basket"));
    a->setStatusTip(tr("Allows you to change current basket to the next one "
                         "without having to open the main window."));
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(addNoteHtml()));
    a->setText(tr("Insert text note"));
    a->setStatusTip(
        tr("Add a text note to the current basket without having to open "
             "the main window."));
    a->setShortcut(QKeySequence(modifier + Qt::Key_T));
    a->setShortcutContext(Qt::ApplicationShortcut);

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(addNoteImage()));
    a->setText(tr("Insert image note"));
    a->setStatusTip(
        tr("Add an image note to the current basket without having to open "
             "the main window."));

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(addNoteLink()));
    a->setText(tr("Insert link note"));
    a->setStatusTip(
        tr("Add a link note to the current basket without having "
             "to open the main window."));

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(addNoteColor()));
    a->setText(tr("Insert color note"));
    a->setStatusTip(
        tr("Add a color note to the current basket without having to open "
             "the main window."));

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(slotColorFromScreenGlobal()));
    a->setText(tr("Pick color from screen"));
    a->setStatusTip(
        tr("Add a color note picked from one pixel on screen to the current "
             "basket without " "having to open the main window."));

    a = new QAction(this->parent());
    connect(a, SIGNAL(triggered()), Global::bnpView, SLOT(grabScreenshotGlobal()));
    a->setText(tr("Grab screen zone"));
    a->setStatusTip(
        tr("Grab a screen zone as an image in the current basket without "
             "having to open the main window."));







//    actQuit         = KStandardAction::quit(this, SLOT(quit()), ac);

//    KAction *a = NULL;
//    a = ac->addAction("minimizeRestore", this,
//                                      SLOT(minimizeRestore()));
//    a->setText(tr("Minimize"));
//    a->setIcon(KIcon(""));
//    a->setShortcut(0);

    /** Settings : ************************************************************/
//    m_actShowToolbar   = KStandardAction::showToolbar(   this, SLOT(toggleToolBar()),   ac);
//    m_actShowStatusbar = KStandardAction::showStatusbar(this, SLOT(toggleStatusBar()), ac);

//    m_actShowToolbar->setCheckedState( KGuiItem(tr("Hide &Toolbar")) );

    (void) KStandardAction::keyBindings(this, SLOT(showShortcutsSettingsDialog()), ac);

    (void) KStandardAction::configureToolbars(this, SLOT(configureToolbars()), ac);

    //KAction *actCfgNotifs = KStandardAction::configureNotifications(this, SLOT(configureNotifications()), ac );
    //actCfgNotifs->setEnabled(false); // Not yet implemented !

    actAppConfig = KStandardAction::preferences(this, SLOT(showSettingsDialog()), ac);
}

void MainWindow::toggleToolBar()
{
    if (m_mainToolBar->isVisible())
        m_mainToolBar->hide();
    else
        m_mainToolBar->show();

//    saveMainWindowSettings( KGlobal::config(), autoSaveGroup() );
}

void MainWindow::toggleStatusBar()
{
    if (statusBar()->isVisible())
        statusBar()->hide();
    else
        statusBar()->show();

//    KConfigGroup group = KGlobal::config()->group(autoSaveGroup());
//    saveMainWindowSettings(group);
}

void MainWindow::configureToolbars()
{
//    KConfigGroup group = KGlobal::config()->group(autoSaveGroup());
//    saveMainWindowSettings(group);

    KEditToolBar dlg(ac);
    connect(&dlg, SIGNAL(newToolbarConfig()), this, SLOT(slotNewToolbarConfig()));
    dlg.exec();
}

void MainWindow::configureNotifications()
{
    // TODO
    // KNotifyDialog *dialog = new KNotifyDialog(this, "KNotifyDialog", false);
    // dialog->show();
}

void MainWindow::slotNewToolbarConfig() // This is called when OK or Apply is clicked
{
    // ...if you use any action list, use plugActionList on each here...
    if (!Global::bnpView->isPart())
        Global::bnpView->connectTagsMenu(); // The Tags menu was created again!
    // TODO: Does this do anything?
//    plugActionList(QString::fromLatin1("go_baskets_list"), actBasketsList);
//    KConfigGroup group = KGlobal::config()->group(autoSaveGroup());
//    applyMainWindowSettings(group);
}

void MainWindow::showSettingsDialog()
{
    if (m_settings == 0)
        m_settings = new KSettings::Dialog(kapp->activeWindow());
    if (Global::mainWindow()) {
        m_settings->showButton(KDialog::Help,    false); // Not implemented!
        m_settings->showButton(KDialog::Default, false); // Not implemented!
        m_settings->exec();
    } else
        m_settings->show();
}

void MainWindow::showShortcutsSettingsDialog()
{
}

void MainWindow::ensurePolished()
{
    bool shouldSave = false;

    // If position and size has never been set, set nice ones:
    //  - Set size to sizeHint()
    //  - Keep the window manager placing the window where it want and save this
    if (Settings::mainWindowSize().isEmpty()) {
//      kDebug() << "Main Window Position: Initial Set in show()";
        int defaultWidth  = kapp->desktop()->width()  * 5 / 6;
        int defaultHeight = kapp->desktop()->height() * 5 / 6;
        resize(defaultWidth, defaultHeight); // sizeHint() is bad (too small) and we want the user to have a good default area size
        shouldSave = true;
    } else {
//      kDebug() << "Main Window Position: Recall in show(x="
//                << Settings::mainWindowPosition().x() << ", y=" << Settings::mainWindowPosition().y()
//                << ", width=" << Settings::mainWindowSize().width() << ", height=" << Settings::mainWindowSize().height()
//                << ")";
        //move(Settings::mainWindowPosition());
        //resize(Settings::mainWindowSize());
    }

    if (shouldSave) {
//      kDebug() << "Main Window Position: Save size and position in show(x="
//                << pos().x() << ", y=" << pos().y()
//                << ", width=" << size().width() << ", height=" << size().height()
//                << ")";
        Settings::setMainWindowPosition(pos());
        Settings::setMainWindowSize(size());
        Settings::saveConfig();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
//  kDebug() << "Main Window Position: Save size in resizeEvent(width=" << size().width() << ", height=" << size().height() << ") ; isMaximized="
//            << (isMaximized() ? "true" : "false");
    Settings::setMainWindowSize(size());
    Settings::saveConfig();

    // Added to make it work (previous lines do not work):
    //saveMainWindowSettings( KGlobal::config(), autoSaveGroup() );
    QMainWindow::resizeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent *event)
{
//  kDebug() << "Main Window Position: Save position in moveEvent(x=" << pos().x() << ", y=" << pos().y() << ")";
    Settings::setMainWindowPosition(pos());
    Settings::saveConfig();

    // Added to make it work (previous lines do not work):
    //saveMainWindowSettings( KGlobal::config(), autoSaveGroup() );
    QMainWindow::moveEvent(event);
}

bool MainWindow::queryExit()
{
    hide();
    return true;
}

void MainWindow::quit()
{
    m_quit = true;
    close();
}

bool MainWindow::queryClose()
{
    /*  if (m_shuttingDown) // Set in askForQuit(): we don't have to ask again
        return true;*/

    if (kapp->sessionSaving()) {
        Settings::setStartDocked(false); // If queryClose() is called it's because the window is shown
        Settings::saveConfig();
        return true;
    }

    if (Settings::useSystray()
            && !m_quit ) {
        hide();
        return false;
    } else
        return askForQuit();
}

bool MainWindow::askForQuit()
{
    QString message = tr("<p>Do you really want to quit %1?</p>").arg(KGlobal::mainComponent().aboutData()->programName());
    if (Settings::useSystray())
        message += tr("<p>Notice that you do not have to quit the application before ending your KDE session. "
                        "If you end your session while the application is still running, the application will be reloaded the next time you log in.</p>");

    int really = QMessageBox::warning(this, tr("Quit Confirm"), message,
                                      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);

    if (really == QMessageBox::Cancel) {
        m_quit = false;
        return false;
    }

    return true;
}

void MainWindow::minimizeRestore()
{
    if (isVisible())
        hide();
    else
        show();
}

