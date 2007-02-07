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

 /// NEW:

#include <qwidgetstack.h>
#include <qregexp.h>
#include <qpixmap.h>
#include <qpainter.h>
#include <qimage.h>
#include <qbitmap.h>
#include <qwhatsthis.h>
#include <kpopupmenu.h>
#include <qsignalmapper.h>
#include <qdir.h>
#include <kicontheme.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kstringhandler.h>
#include <kmessagebox.h>
#include <kfiledialog.h>
#include <kprogress.h>
#include <kstandarddirs.h>
#include <kaboutdata.h>
#include <kwin.h>
#include <kaccel.h>
#include <kpassivepopup.h>
#include <kxmlguifactory.h>
#include <kcmdlineargs.h>
#include <kglobalaccel.h>
#include <kapplication.h>
#include <kkeydialog.h>
#include <dcopclient.h>
#include <kdebug.h>
#include <iostream>
#include "bnpview.h"
#include "basket.h"
#include "tools.h"
#include "settings.h"
#include "debugwindow.h"
#include "xmlwork.h"
#include "basketfactory.h"
#include "softwareimporters.h"
#include "colorpicker.h"
#include "regiongrabber.h"
#include "basketlistview.h"
#include "basketproperties.h"
#include "password.h"
#include "newbasketdialog.h"
#include "notedrag.h"
#include "formatimporter.h"
#include "basketstatusbar.h"
#include "backgroundmanager.h"
#include "noteedit.h" // To launch InlineEditors::initToolBars()
#include "archive.h"
#include "htmlexporter.h"
#include "crashhandler.h"
#include "likeback.h"
#include "backup.h"

/** class BNPView: */

const int BNPView::c_delayTooltipTime = 275;

BNPView::BNPView(QWidget *parent, const char *name, KXMLGUIClient *aGUIClient,
				 KActionCollection *actionCollection, BasketStatusBar *bar)
	: DCOPObject("BasketIface"), QSplitter(Qt::Horizontal, parent, name), m_actLockBasket(0), m_actPassBasket(0),
	m_loading(true), m_newBasketPopup(false), m_firstShow(true),
	m_regionGrabber(0), m_passiveDroppedSelection(0), m_passivePopup(0), m_actionCollection(actionCollection),
	m_guiClient(aGUIClient), m_statusbar(bar), m_tryHideTimer(0), m_hideTimer(0)
{
	/* Settings */
	Settings::loadConfig();

	Global::bnpView = this;

	// Needed when loading the baskets:
	Global::globalAccel       = new KGlobalAccel(this); // FIXME: might be null (KPart case)!
	Global::backgroundManager = new BackgroundManager();

	setupGlobalShortcuts();
	initialize();
	QTimer::singleShot(0, this, SLOT(lateInit()));
}

BNPView::~BNPView()
{
    int treeWidth = Global::bnpView->sizes()[Settings::treeOnLeft() ? 0 : 1];

    Settings::setBasketTreeWidth(treeWidth);

    if (currentBasket() && currentBasket()->isDuringEdit())
		currentBasket()->closeEditor();

	Settings::saveConfig();

	Global::bnpView = 0;

	delete Global::systemTray;
	Global::systemTray = 0;
	delete m_colorPicker;
	delete m_statusbar;

	NoteDrag::createAndEmptyCuttingTmpFolder(); // Clean the temporary folder we used
}

void BNPView::lateInit()
{
/*
	InlineEditors* instance = InlineEditors::instance();

	if(instance)
	{
		KToolBar* toolbar = instance->richTextToolBar();

		if(toolbar)
			toolbar->hide();
	}
*/
	if(!isPart())
	{
		if (Settings::useSystray() && KCmdLineArgs::parsedArgs() && KCmdLineArgs::parsedArgs()->isSet("start-hidden"))
			if(Global::mainWindow()) Global::mainWindow()->hide();
		else if (Settings::useSystray() && kapp->isRestored())
			if(Global::mainWindow()) Global::mainWindow()->setShown(!Settings::startDocked());
		else
			showMainWindow();
	}

	// If the main window is hidden when session is saved, Container::queryClose()
	//  isn't called and the last value would be kept
	Settings::setStartDocked(true);
	Settings::saveConfig();

	/* System tray icon */
	Global::systemTray = new SystemTray(Global::mainWindow());
	connect( Global::systemTray, SIGNAL(showPart()), this, SIGNAL(showPart()) );
	if (Settings::useSystray())
		Global::systemTray->show();

	// Load baskets
	DEBUG_WIN << "Baskets are loaded from " + Global::basketsFolder();

	NoteDrag::createAndEmptyCuttingTmpFolder(); // If last exec hasn't done it: clean the temporary folder we will use
	Tag::loadTags(); // Tags should be ready before loading baskets, but tags need the mainContainer to be ready to create KActions!
	load();

	// If no basket has been found, try to import from an older version,
	if (!firstListViewItem()) {
		QDir dir;
		dir.mkdir(Global::basketsFolder());
		if (FormatImporter::shouldImportBaskets()) {
			FormatImporter::importBaskets();
			load();
		}
		if (!firstListViewItem()) {
		// Create first basket:
			BasketFactory::newBasket(/*icon=*/"", /*name=*/i18n("General"), /*backgroundImage=*/"", /*backgroundColor=*/QColor(), /*textColor=*/QColor(), /*templateName=*/"1column", /*createIn=*/0);
		}
	}

	// Load the Welcome Baskets if it is the First Time:
	if (!Settings::welcomeBasketsAdded()) {
		addWelcomeBaskets();
		Settings::setWelcomeBasketsAdded(true);
		Settings::saveConfig();
	}

	m_tryHideTimer = new QTimer(this);
	m_hideTimer    = new QTimer(this);
	connect( m_tryHideTimer, SIGNAL(timeout()), this, SLOT(timeoutTryHide()) );
	connect( m_hideTimer,    SIGNAL(timeout()), this, SLOT(timeoutHide())    );
}

void BNPView::addWelcomeBaskets()
{
	// Possible paths where to find the welcome basket archive, trying the translated one, and falling back to the English one:
	QStringList possiblePaths;
	if (QString(KGlobal::locale()->encoding()) == QString("UTF-8")) { // Welcome baskets are encoded in UTF-8. If the system is not, then use the English version:
		possiblePaths.append(KGlobal::dirs()->findResource("data", "basket/welcome/Welcome_" + KGlobal::locale()->language() + ".baskets"));
		possiblePaths.append(KGlobal::dirs()->findResource("data", "basket/welcome/Welcome_" + QStringList::split("_", KGlobal::locale()->language())[0] + ".baskets"));
	}
	possiblePaths.append(KGlobal::dirs()->findResource("data", "basket/welcome/Welcome_en_US.baskets"));

	// Take the first EXISTING basket archive found:
	QDir dir;
	QString path;
	for (QStringList::Iterator it = possiblePaths.begin(); it != possiblePaths.end(); ++it) {
		if (dir.exists(*it)) {
			path = *it;
			break;
		}
	}

	// Extract:
	if (!path.isEmpty())
		Archive::open(path);
}

void BNPView::onFirstShow()
{
	// Don't enable LikeBack until bnpview is shown. This way it works better with kontact.
	/* LikeBack */
/*	Global::likeBack = new LikeBack(LikeBack::AllButtons, / *showBarByDefault=* /true, Global::config(), Global::about());
	Global::likeBack->setServer("basket.linux62.org", "/likeback/send.php");
	Global:likeBack->setAcceptedLanguages(QStringList::split(";", "en;fr"), i18n("Only english and french languages are accepted."));
	if (isPart())
		Global::likeBack->disableBar(); // See BNPView::shown() and BNPView::hide().
*/

	if (isPart())
		Global::likeBack->disableBar(); // See BNPView::shown() and BNPView::hide().

/*
	LikeBack::init(Global::config(), Global::about(), LikeBack::AllButtons);
	LikeBack::setServer("basket.linux62.org", "/likeback/send.php");
//	LikeBack::setServer("localhost", "/~seb/basket/likeback/send.php");
	LikeBack::setCustomLanguageMessage(i18n("Only english and french languages are accepted."));
//	LikeBack::setWindowNamesListing(LikeBack:: / *NoListing* / / *WarnUnnamedWindows* / AllWindows);
*/

	// In late init, because we need kapp->mainWidget() to be set!
	if (!isPart())
		connectTagsMenu();

	m_statusbar->setupStatusBar();

    int treeWidth = Settings::basketTreeWidth();
    if (treeWidth < 0)
      treeWidth = m_tree->fontMetrics().maxWidth() * 11;
    QValueList<int> splitterSizes;
    splitterSizes.append(treeWidth);
    setSizes(splitterSizes);
}

void BNPView::setupGlobalShortcuts()
{
	/* Global shortcuts */
	KGlobalAccel *globalAccel = Global::globalAccel; // Better for the following lines

	// Ctrl+Shift+W only works when started standalone:
	QWidget *basketMainWindow = (QWidget*) (Global::bnpView->parent()->inherits("MainWindow") ? Global::bnpView->parent() : 0);

	if (basketMainWindow) {
		globalAccel->insert( "global_show_hide_main_window", i18n("Show/hide main window"),
							i18n("Allows you to show main Window if it is hidden, and to hide it if it is shown."),
							Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_W, Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_W,
							basketMainWindow, SLOT(changeActive()),           true, true );
	}
	globalAccel->insert( "global_paste", i18n("Paste clipboard contents in current basket"),
						 i18n("Allows you to paste clipboard contents in the current basket without having to open the main window."),
						 Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_V, Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_V,
						 Global::bnpView, SLOT(globalPasteInCurrentBasket()), true, true );
	globalAccel->insert( "global_show_current_basket", i18n("Show current basket name"),
						 i18n("Allows you to know basket is current without opening the main window."),
						 "", "",
						 Global::bnpView, SLOT(showPassiveContentForced()), true, true );
	globalAccel->insert( "global_paste_selection", i18n("Paste selection in current basket"),
						 i18n("Allows you to paste clipboard selection in the current basket without having to open the main window."),
						 Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_S, Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_S,
						 Global::bnpView, SLOT(pasteSelInCurrentBasket()),  true, true );
	globalAccel->insert( "global_new_basket", i18n("Create a new basket"),
						 i18n("Allows you to create a new basket without having to open the main window (you then can use the other global shortcuts to add a note, paste clipboard or paste selection in this new basket)."),
						 "", "",
						 Global::bnpView, SLOT(askNewBasket()),       true, true );
	globalAccel->insert( "global_previous_basket", i18n("Go to previous basket"),
						 i18n("Allows you to change current basket to the previous one without having to open the main window."),
						 "", "",
						 Global::bnpView,    SLOT(goToPreviousBasket()), true, true );
	globalAccel->insert( "global_next_basket", i18n("Go to next basket"),
						 i18n("Allows you to change current basket to the next one without having to open the main window."),
						 "", "",
						 Global::bnpView,    SLOT(goToNextBasket()),     true, true );
//	globalAccel->insert( "global_note_add_text", i18n("Insert plain text note"),
//						 i18n("Add a plain text note to the current basket without having to open the main window."),
//						 "", "", //Qt::CTRL+Qt::ALT+Qt::Key_T, Qt::CTRL+Qt::ALT+Qt::Key_T,
//						 Global::bnpView, SLOT(addNoteText()),        true, true );
	globalAccel->insert( "global_note_add_html", i18n("Insert text note"),
						 i18n("Add a text note to the current basket without having to open the main window."),
						 Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_T, Qt::CTRL + Qt::ALT + Qt::SHIFT + Qt::Key_T, //"", "",
						 Global::bnpView, SLOT(addNoteHtml()),        true, true );
	globalAccel->insert( "global_note_add_image", i18n("Insert image note"),
						 i18n("Add an image note to the current basket without having to open the main window."),
						 "", "",
						 Global::bnpView, SLOT(addNoteImage()),       true, true );
	globalAccel->insert( "global_note_add_link", i18n("Insert link note"),
						 i18n("Add a link note to the current basket without having to open the main window."),
						 "", "",
						 Global::bnpView, SLOT(addNoteLink()),        true, true );
	globalAccel->insert( "global_note_add_color", i18n("Insert color note"),
						 i18n("Add a color note to the current basket without having to open the main window."),
						 "", "",
						 Global::bnpView, SLOT(addNoteColor()),       true, true );
	globalAccel->insert( "global_note_pick_color", i18n("Pick color from screen"),
						 i18n("Add a color note picked from one pixel on screen to the current basket without "
								 "having to open the main window."),
						 "", "",
						 Global::bnpView, SLOT(slotColorFromScreenGlobal()), true, true );
	globalAccel->insert( "global_note_grab_screenshot", i18n("Grab screen zone"),
						 i18n("Grab a screen zone as an image in the current basket without "
								 "having to open the main window."),
						 "", "",
						 Global::bnpView, SLOT(grabScreenshotGlobal()), true, true );
	globalAccel->readSettings();
	globalAccel->updateConnections();
}

void BNPView::initialize()
{
	/// Configure the List View Columns:
	m_tree  = new BasketTreeListView(this);
	m_tree->addColumn(i18n("Baskets"));
	m_tree->setColumnWidthMode(0, QListView::Maximum);
	m_tree->setFullWidth(true);
	m_tree->setSorting(-1/*Disabled*/);
	m_tree->setRootIsDecorated(true);
	m_tree->setTreeStepSize(16);
	m_tree->setLineWidth(1);
	m_tree->setMidLineWidth(0);
	m_tree->setFocusPolicy(QWidget::NoFocus);

	/// Configure the List View Drag and Drop:
	m_tree->setDragEnabled(true);
	m_tree->setAcceptDrops(true);
	m_tree->setItemsMovable(true);
	m_tree->setDragAutoScroll(true);
	m_tree->setDropVisualizer(true);
	m_tree->setDropHighlighter(true);

	/// Configure the Splitter:
	m_stack = new QWidgetStack(this);

	setOpaqueResize(true);

	setCollapsible(m_tree,  true);
	setCollapsible(m_stack, false);
	setResizeMode(m_tree,  QSplitter::KeepSize);
	setResizeMode(m_stack, QSplitter::Stretch);

	/// Configure the List View Signals:
	connect( m_tree, SIGNAL(returnPressed(QListViewItem*)),    this, SLOT(slotPressed(QListViewItem*)) );
	connect( m_tree, SIGNAL(selectionChanged(QListViewItem*)), this, SLOT(slotPressed(QListViewItem*)) );
	connect( m_tree, SIGNAL(pressed(QListViewItem*)),          this, SLOT(slotPressed(QListViewItem*)) );
	connect( m_tree, SIGNAL(expanded(QListViewItem*)),         this, SLOT(needSave(QListViewItem*))    );
	connect( m_tree, SIGNAL(collapsed(QListViewItem*)),        this, SLOT(needSave(QListViewItem*))    );
	connect( m_tree, SIGNAL(contextMenu(KListView*, QListViewItem*, const QPoint&)),      this, SLOT(slotContextMenu(KListView*, QListViewItem*, const QPoint&))      );
	connect( m_tree, SIGNAL(mouseButtonPressed(int, QListViewItem*, const QPoint&, int)), this, SLOT(slotMouseButtonPressed(int, QListViewItem*, const QPoint&, int)) );
	connect( m_tree, SIGNAL(doubleClicked(QListViewItem*, const QPoint&, int)), this, SLOT(slotShowProperties(QListViewItem*, const QPoint&, int)) );

	connect( m_tree, SIGNAL(expanded(QListViewItem*)),  this, SIGNAL(basketChanged()) );
	connect( m_tree, SIGNAL(collapsed(QListViewItem*)), this, SIGNAL(basketChanged()) );
	connect( this,   SIGNAL(basketNumberChanged(int)),  this, SIGNAL(basketChanged()) );

	connect( this, SIGNAL(basketNumberChanged(int)), this, SLOT(slotBasketNumberChanged(int)) );
	connect( this, SIGNAL(basketChanged()),          this, SLOT(slotBasketChanged())          );

	/* LikeBack */
	Global::likeBack = new LikeBack(LikeBack::AllButtons, /*showBarByDefault=*/false, Global::config(), Global::about());
	Global::likeBack->setServer("basket.linux62.org", "/likeback/send.php");

// There are too much comments, and people reading comments are more and more international, so we accept only English:
//	Global::likeBack->setAcceptedLanguages(QStringList::split(";", "en;fr"), i18n("Please write in English or French."));

//	if (isPart())
//		Global::likeBack->disableBar(); // See BNPView::shown() and BNPView::hide().

	Global::likeBack->sendACommentAction(actionCollection()); // Just create it!
	setupActions();

	/// What's This Help for the tree:
	QWhatsThis::add(m_tree, i18n(
			"<h2>Basket Tree</h2>"
					"Here is the list of your baskets. "
					"You can organize your data by putting them in different baskets. "
					"You can group baskets by subject by creating new baskets inside others. "
					"You can browse between them by clicking a basket to open it, or reorganize them using drag and drop."));

	setTreePlacement(Settings::treeOnLeft());
}

void BNPView::setupActions()
{
	m_actSaveAsArchive = new KAction( i18n("&Basket Archive..."),  "baskets", 0,
	                                  this, SLOT(saveAsArchive()), actionCollection(), "basket_export_basket_archive" );
	m_actOpenArchive   = new KAction( i18n("&Basket Archive..."),  "baskets", 0,
	                                  this, SLOT(openArchive()),   actionCollection(), "basket_import_basket_archive" );

	m_actHideWindow = new KAction( i18n("&Hide Window"), "", KStdAccel::shortcut(KStdAccel::Close),
								   this, SLOT(hideOnEscape()), actionCollection(), "window_hide" );
	m_actHideWindow->setEnabled(Settings::useSystray()); // Init here !

	m_actExportToHtml = new KAction( i18n("&HTML Web Page..."), "html", 0,
	             this, SLOT(exportToHTML()),      actionCollection(), "basket_export_html" );
	new KAction( i18n("K&Notes"), "knotes", 0,
	             this, SLOT(importKNotes()),      actionCollection(), "basket_import_knotes" );
	new KAction( i18n("K&Jots"), "kjots", 0,
	             this, SLOT(importKJots()),       actionCollection(), "basket_import_kjots" );
	new KAction( i18n("&KnowIt..."), "knowit", 0,
	             this, SLOT(importKnowIt()),      actionCollection(), "basket_import_knowit" );
	new KAction( i18n("Tux&Cards..."), "tuxcards", 0,
	             this, SLOT(importTuxCards()),    actionCollection(), "basket_import_tuxcards" );
	new KAction( i18n("&Sticky Notes"), "gnome", 0,
	             this, SLOT(importStickyNotes()), actionCollection(), "basket_import_sticky_notes" );
	new KAction( i18n("&Tomboy"), "tintin", 0,
	             this, SLOT(importTomboy()),      actionCollection(), "basket_import_tomboy" );
	new KAction( i18n("Text &File..."), "txt", 0,
	             this, SLOT(importTextFile()),    actionCollection(), "basket_import_text_file" );

	new KAction( i18n("&Backup && Restore..."), "", 0,
	             this, SLOT(backupRestore()), actionCollection(), "basket_backup_restore" );

	/** Note : ****************************************************************/

	m_actDelNote  = new KAction( i18n("D&elete"), "editdelete", "Delete",
								 this, SLOT(delNote()), actionCollection(), "edit_delete" );
	m_actCutNote  = KStdAction::cut(   this, SLOT(cutNote()),               actionCollection() );
	m_actCopyNote = KStdAction::copy(  this, SLOT(copyNote()),              actionCollection() );

	m_actSelectAll = KStdAction::selectAll( this, SLOT( slotSelectAll() ), actionCollection() );
	m_actSelectAll->setStatusText( i18n( "Selects all notes" ) );
	m_actUnselectAll = new KAction( i18n( "U&nselect All" ), "", this, SLOT( slotUnselectAll() ),
									actionCollection(), "edit_unselect_all" );
	m_actUnselectAll->setStatusText( i18n( "Unselects all selected notes" ) );
	m_actInvertSelection = new KAction( i18n( "&Invert Selection" ), CTRL+Key_Asterisk,
										this, SLOT( slotInvertSelection() ),
										actionCollection(), "edit_invert_selection" );
	m_actInvertSelection->setStatusText( i18n( "Inverts the current selection of notes" ) );

	m_actEditNote         = new KAction( i18n("Verb; not Menu", "&Edit..."), "edit",   "Return",
										 this, SLOT(editNote()), actionCollection(), "note_edit" );

	m_actOpenNote         = KStdAction::open( this, SLOT(openNote()), actionCollection(), "note_open" );
	m_actOpenNote->setIcon("edit");
	m_actOpenNote->setText(i18n("&Open"));
	m_actOpenNote->setShortcut("F9");

	m_actOpenNoteWith     = new KAction( i18n("Open &With..."), "", "Shift+F9",
										 this, SLOT(openNoteWith()), actionCollection(), "note_open_with" );
	m_actSaveNoteAs       = KStdAction::saveAs( this, SLOT(saveNoteAs()), actionCollection(), "note_save_to_file" );
	m_actSaveNoteAs->setIcon("");
	m_actSaveNoteAs->setText(i18n("&Save to File..."));
	m_actSaveNoteAs->setShortcut("F10");

	m_actGroup        = new KAction( i18n("&Group"),          "attach",     "Ctrl+G",
									 this, SLOT(noteGroup()),    actionCollection(), "note_group" );
	m_actUngroup      = new KAction( i18n("U&ngroup"),        "",           "Ctrl+Shift+G",
									 this, SLOT(noteUngroup()),  actionCollection(), "note_ungroup" );

	m_actMoveOnTop    = new KAction( i18n("Move on &Top"),    "2uparrow",   "Ctrl+Shift+Home",
									 this, SLOT(moveOnTop()),    actionCollection(), "note_move_top" );
	m_actMoveNoteUp   = new KAction( i18n("Move &Up"),        "1uparrow",   "Ctrl+Shift+Up",
									 this, SLOT(moveNoteUp()),   actionCollection(), "note_move_up" );
	m_actMoveNoteDown = new KAction( i18n("Move &Down"),      "1downarrow", "Ctrl+Shift+Down",
									 this, SLOT(moveNoteDown()), actionCollection(), "note_move_down" );
	m_actMoveOnBottom = new KAction( i18n("Move on &Bottom"), "2downarrow", "Ctrl+Shift+End",
									 this, SLOT(moveOnBottom()), actionCollection(), "note_move_bottom" );
#if KDE_IS_VERSION( 3, 1, 90 ) // KDE 3.2.x
	m_actPaste = KStdAction::pasteText( this, SLOT(pasteInCurrentBasket()), actionCollection() );
#else
	m_actPaste = KStdAction::paste(     this, SLOT(pasteInCurrentBasket()), actionCollection() );
#endif

	/** Insert : **************************************************************/

	QSignalMapper *insertEmptyMapper  = new QSignalMapper(this);
	QSignalMapper *insertWizardMapper = new QSignalMapper(this);
	connect( insertEmptyMapper,  SIGNAL(mapped(int)), this, SLOT(insertEmpty(int))  );
	connect( insertWizardMapper, SIGNAL(mapped(int)), this, SLOT(insertWizard(int)) );

//	m_actInsertText   = new KAction( i18n("Plai&n Text"), "text",     "Ctrl+T", actionCollection(), "insert_text"     );
	m_actInsertHtml   = new KAction( i18n("&Text"),       "html",     "Insert", actionCollection(), "insert_html"     );
	m_actInsertLink   = new KAction( i18n("&Link"),       "link",     "Ctrl+Y", actionCollection(), "insert_link"     );
	m_actInsertImage  = new KAction( i18n("&Image"),      "image",    "",       actionCollection(), "insert_image"    );
	m_actInsertColor  = new KAction( i18n("&Color"),      "colorset", "",       actionCollection(), "insert_color"    );
	m_actInsertLauncher=new KAction( i18n("L&auncher"),   "launch",   "",       actionCollection(), "insert_launcher" );

	m_actImportKMenu  = new KAction( i18n("Import Launcher from &KDE Menu..."), "kmenu",      "", actionCollection(), "insert_kmenu"     );
	m_actImportIcon   = new KAction( i18n("Im&port Icon..."),                   "icons",      "", actionCollection(), "insert_icon"      );
	m_actLoadFile     = new KAction( i18n("Load From &File..."),                "fileimport", "", actionCollection(), "insert_from_file" );

//	connect( m_actInsertText,     SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertHtml,     SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertImage,    SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertLink,     SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertColor,    SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertLauncher, SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
//	insertEmptyMapper->setMapping(m_actInsertText,     NoteType::Text    );
	insertEmptyMapper->setMapping(m_actInsertHtml,     NoteType::Html    );
	insertEmptyMapper->setMapping(m_actInsertImage,    NoteType::Image   );
	insertEmptyMapper->setMapping(m_actInsertLink,     NoteType::Link    );
	insertEmptyMapper->setMapping(m_actInsertColor,    NoteType::Color   );
	insertEmptyMapper->setMapping(m_actInsertLauncher, NoteType::Launcher);

	connect( m_actImportKMenu, SIGNAL(activated()), insertWizardMapper, SLOT(map()) );
	connect( m_actImportIcon,  SIGNAL(activated()), insertWizardMapper, SLOT(map()) );
	connect( m_actLoadFile,    SIGNAL(activated()), insertWizardMapper, SLOT(map()) );
	insertWizardMapper->setMapping(m_actImportKMenu,  1 );
	insertWizardMapper->setMapping(m_actImportIcon,   2 );
	insertWizardMapper->setMapping(m_actLoadFile,     3 );

	m_colorPicker = new DesktopColorPicker();
	m_actColorPicker = new KAction( i18n("C&olor from Screen"), "kcolorchooser", "",
									this, SLOT(slotColorFromScreen()), actionCollection(), "insert_screen_color" );
	connect( m_colorPicker, SIGNAL(pickedColor(const QColor&)), this, SLOT(colorPicked(const QColor&)) );
	connect( m_colorPicker, SIGNAL(canceledPick()),             this, SLOT(colorPickingCanceled())     );

	m_actGrabScreenshot = new KAction( i18n("Grab Screen &Zone"), "ksnapshot", "",
									   this, SLOT(grabScreenshot()), actionCollection(), "insert_screen_capture" );
	//connect( m_actGrabScreenshot, SIGNAL(regionGrabbed(const QPixmap&)), this, SLOT(screenshotGrabbed(const QPixmap&)) );
	//connect( m_colorPicker, SIGNAL(canceledPick()),             this, SLOT(colorPickingCanceled())     );

//	m_insertActions.append( m_actInsertText     );
	m_insertActions.append( m_actInsertHtml     );
	m_insertActions.append( m_actInsertLink     );
	m_insertActions.append( m_actInsertImage    );
	m_insertActions.append( m_actInsertColor    );
	m_insertActions.append( m_actImportKMenu    );
	m_insertActions.append( m_actInsertLauncher );
	m_insertActions.append( m_actImportIcon     );
	m_insertActions.append( m_actLoadFile       );
	m_insertActions.append( m_actColorPicker    );
	m_insertActions.append( m_actGrabScreenshot );

	/** Basket : **************************************************************/

	// At this stage, main.cpp has not set kapp->mainWidget(), so Global::runInsideKontact()
	// returns true. We do it ourself:
	bool runInsideKontact = true;
	QWidget *parentWidget = (QWidget*) parent();
	while (parentWidget) {
		if (parentWidget->inherits("MainWindow"))
			runInsideKontact = false;
		parentWidget = (QWidget*) parentWidget->parent();
	}

	// Use the "basket" incon in Kontact so it is consistent with the Kontact "New..." icon
	actNewBasket        = new KAction( i18n("&New Basket..."), (runInsideKontact ? "basket" : "filenew"), KStdAccel::shortcut(KStdAccel::New),
									   this, SLOT(askNewBasket()), actionCollection(), "basket_new" );
	actNewSubBasket     = new KAction( i18n("New &Sub-Basket..."), "", "Ctrl+Shift+N",
									   this, SLOT(askNewSubBasket()), actionCollection(), "basket_new_sub" );
	actNewSiblingBasket = new KAction( i18n("New Si&bling Basket..."), "", "",
									   this, SLOT(askNewSiblingBasket()), actionCollection(), "basket_new_sibling" );

	KActionMenu *newBasketMenu = new KActionMenu(i18n("&New"), "filenew", actionCollection(), "basket_new_menu");
	newBasketMenu->insert(actNewBasket);
	newBasketMenu->insert(actNewSubBasket);
	newBasketMenu->insert(actNewSiblingBasket);
	connect( newBasketMenu, SIGNAL(activated()), this, SLOT(askNewBasket()) );

	m_actPropBasket = new KAction( i18n("&Properties..."), "misc", "F2",
								   this, SLOT(propBasket()), actionCollection(), "basket_properties" );
	m_actDelBasket  = new KAction( i18n("Remove Basket", "&Remove"), "", 0,
								   this, SLOT(delBasket()), actionCollection(), "basket_remove" );
#ifdef HAVE_LIBGPGME
	m_actPassBasket = new KAction( i18n("Password protection", "Pass&word..."), "", 0,
								   this, SLOT(password()), actionCollection(), "basket_password" );
	m_actLockBasket = new KAction( i18n("Lock Basket", "&Lock"), "", "Ctrl+L",
								   this, SLOT(lockBasket()), actionCollection(), "basket_lock" );
#endif
	/** Edit : ****************************************************************/

	//m_actUndo     = KStdAction::undo(  this, SLOT(undo()),                 actionCollection() );
	//m_actUndo->setEnabled(false); // Not yet implemented !
	//m_actRedo     = KStdAction::redo(  this, SLOT(redo()),                 actionCollection() );
	//m_actRedo->setEnabled(false); // Not yet implemented !

	m_actShowFilter  = new KToggleAction( i18n("&Filter"), "filter", KStdAccel::shortcut(KStdAccel::Find),
										  actionCollection(), "edit_filter" );
	connect( m_actShowFilter, SIGNAL(toggled(bool)), this, SLOT(showHideFilterBar(bool)) );

	m_actFilterAllBaskets = new KToggleAction( i18n("Filter all &Baskets"), "find", "Ctrl+Shift+F",
											   actionCollection(), "edit_filter_all_baskets" );
	connect( m_actFilterAllBaskets, SIGNAL(toggled(bool)), this, SLOT(toggleFilterAllBaskets(bool)) );

	m_actResetFilter = new KAction( i18n( "&Reset Filter" ), "locationbar_erase", "Ctrl+R",
									this, SLOT( slotResetFilter() ), actionCollection(), "edit_filter_reset" );

	/** Go : ******************************************************************/

	m_actPreviousBasket = new KAction( i18n( "&Previous Basket" ), "up",      "Alt+Up",
									   this, SLOT(goToPreviousBasket()), actionCollection(), "go_basket_previous" );
	m_actNextBasket     = new KAction( i18n( "&Next Basket" ),     "down",    "Alt+Down",
									   this, SLOT(goToNextBasket()),     actionCollection(), "go_basket_next"     );
	m_actFoldBasket     = new KAction( i18n( "&Fold Basket" ),     "back",    "Alt+Left",
									   this, SLOT(foldBasket()),         actionCollection(), "go_basket_fold"     );
	m_actExpandBasket   = new KAction( i18n( "&Expand Basket" ),   "forward", "Alt+Right",
									   this, SLOT(expandBasket()),       actionCollection(), "go_basket_expand"   );
	// FOR_BETA_PURPOSE:
//	m_convertTexts = new KAction( i18n("Convert text notes to rich text notes"), "compfile", "",
//								  this, SLOT(convertTexts()), actionCollection(), "beta_convert_texts" );

	InlineEditors::instance()->initToolBars(actionCollection());

	actConfigGlobalShortcuts = KStdAction::keyBindings(this, SLOT(showGlobalShortcutsSettingsDialog()),
			actionCollection(), "options_configure_global_keybinding");
	actConfigGlobalShortcuts->setText(i18n("Configure &Global Shortcuts..."));

	/** Help : ****************************************************************/

	new KAction( i18n("&Welcome Baskets"), "", "", this, SLOT(addWelcomeBaskets()), actionCollection(), "help_welcome_baskets" );
}

QListViewItem* BNPView::firstListViewItem()
{
	return m_tree->firstChild();
}

void BNPView::slotShowProperties(QListViewItem *item, const QPoint&, int)
{
	if (item)
		propBasket();
}

void BNPView::slotMouseButtonPressed(int button, QListViewItem *item, const QPoint &/*pos*/, int /*column*/)
{
	if (item && (button & Qt::MidButton)) {
		// TODO: Paste into ((BasketListViewItem*)listViewItem)->basket()
	}
}

void BNPView::slotContextMenu(KListView */*listView*/, QListViewItem *item, const QPoint &pos)
{
	QString menuName;
	if (item) {
		Basket* basket = ((BasketListViewItem*)item)->basket();

		setCurrentBasket(basket);
		menuName = "basket_popup";
	} else {
		menuName = "tab_bar_popup";
		/*
		* "File -> New" create a new basket with the same parent basket as the the current one.
		* But when invoked when right-clicking the empty area at the bottom of the basket tree,
		* it is obvious the user want to create a new basket at the bottom of the tree (with no parent).
		* So we set a temporary variable during the time the popup menu is shown,
		 * so the slot askNewBasket() will do the right thing:
		*/
		setNewBasketPopup();
	}

	QPopupMenu *menu = popupMenu(menuName);
	connect( menu, SIGNAL(aboutToHide()),  this, SLOT(aboutToHideNewBasketPopup()) );
	menu->exec(pos);
}

void BNPView::save()
{
	DEBUG_WIN << "Basket Tree: Saving...";

	// Create Document:
	QDomDocument document("basketTree");
	QDomElement root = document.createElement("basketTree");
	document.appendChild(root);

	// Save Basket Tree:
	save(m_tree->firstChild(), document, root);

	// Write to Disk:
	Basket::safelySaveToFile(Global::basketsFolder() + "baskets.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n" + document.toString());
// 	QFile file(Global::basketsFolder() + "baskets.xml");
// 	if (file.open(IO_WriteOnly)) {
// 		QTextStream stream(&file);
// 		stream.setEncoding(QTextStream::UnicodeUTF8);
// 		QString xml = document.toString();
// 		stream << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
// 		stream << xml;
// 		file.close();
// 	}
}

void BNPView::save(QListViewItem *firstItem, QDomDocument &document, QDomElement &parentElement)
{
	QListViewItem *item = firstItem;
	while (item) {
//		Basket *basket = ((BasketListViewItem*)item)->basket();
		QDomElement basketElement = this->basketElement(item, document, parentElement);
/*
		QDomElement basketElement = document.createElement("basket");
		parentElement.appendChild(basketElement);
		// Save Attributes:
		basketElement.setAttribute("folderName", basket->folderName());
		if (item->firstChild()) // If it can be expanded/folded:
			basketElement.setAttribute("folded", XMLWork::trueOrFalse(!item->isOpen()));
		if (((BasketListViewItem*)item)->isCurrentBasket())
			basketElement.setAttribute("lastOpened", "true");
		// Save Properties:
		QDomElement properties = document.createElement("properties");
		basketElement.appendChild(properties);
		basket->saveProperties(document, properties);
*/
		// Save Child Basket:
		if (item->firstChild())
			save(item->firstChild(), document, basketElement);
		// Next Basket:
		item = item->nextSibling();
	}
}

QDomElement BNPView::basketElement(QListViewItem *item, QDomDocument &document, QDomElement &parentElement)
{
	Basket *basket = ((BasketListViewItem*)item)->basket();
	QDomElement basketElement = document.createElement("basket");
	parentElement.appendChild(basketElement);
	// Save Attributes:
	basketElement.setAttribute("folderName", basket->folderName());
	if (item->firstChild()) // If it can be expanded/folded:
		basketElement.setAttribute("folded", XMLWork::trueOrFalse(!item->isOpen()));
	if (((BasketListViewItem*)item)->isCurrentBasket())
		basketElement.setAttribute("lastOpened", "true");
	// Save Properties:
	QDomElement properties = document.createElement("properties");
	basketElement.appendChild(properties);
	basket->saveProperties(document, properties);
	return basketElement;
}

void BNPView::saveSubHierarchy(QListViewItem *item, QDomDocument &document, QDomElement &parentElement, bool recursive)
{
	QDomElement element = basketElement(item, document, parentElement);
	if (recursive && item->firstChild())
		save(item->firstChild(), document, element);
}

void BNPView::load()
{
	QDomDocument *doc = XMLWork::openFile("basketTree", Global::basketsFolder() + "baskets.xml");
	//BEGIN Compatibility with 0.6.0 Pre-Alpha versions:
	if (!doc)
		doc = XMLWork::openFile("basketsTree", Global::basketsFolder() + "baskets.xml");
	//END
	if (doc != 0) {
		QDomElement docElem = doc->documentElement();
		load(m_tree, 0L, docElem);
	}
	m_loading = false;
}

void BNPView::load(KListView */*listView*/, QListViewItem *item, const QDomElement &baskets)
{
	QDomNode n = baskets.firstChild();
	while ( ! n.isNull() ) {
		QDomElement element = n.toElement();
		if ( (!element.isNull()) && element.tagName() == "basket" ) {
			QString folderName = element.attribute("folderName");
			if (!folderName.isEmpty()) {
				Basket *basket = loadBasket(folderName);
				BasketListViewItem *basketItem = appendBasket(basket, item);
				basketItem->setOpen(!XMLWork::trueOrFalse(element.attribute("folded", "false"), false));
				basket->loadProperties(XMLWork::getElement(element, "properties"));
				if (XMLWork::trueOrFalse(element.attribute("lastOpened", element.attribute("lastOpenned", "false")), false)) // Compat with 0.6.0-Alphas
					setCurrentBasket(basket);
				// Load Sub-baskets:
				load(/*(QListView*)*/0L, basketItem, element);
			}
		}
		n = n.nextSibling();
	}
}

Basket* BNPView::loadBasket(const QString &folderName)
{
	if (folderName.isEmpty())
		return 0;

	DecoratedBasket *decoBasket = new DecoratedBasket(m_stack, folderName);
	Basket          *basket     = decoBasket->basket();
	m_stack->addWidget(decoBasket);
	connect( basket, SIGNAL(countsChanged(Basket*)), this, SLOT(countsChanged(Basket*)) );
	// Important: Create listViewItem and connect signal BEFORE loadProperties(), so we get the listViewItem updated without extra work:
	connect( basket, SIGNAL(propertiesChanged(Basket*)), this, SLOT(updateBasketListViewItem(Basket*)) );

	connect( basket->decoration()->filterBar(), SIGNAL(newFilter(const FilterData&)), this, SLOT(newFilterFromFilterBar()) );

	return basket;
}

int BNPView::basketCount(QListViewItem *parent)
{
	int count = 0;

	QListViewItem *item = (parent ? parent->firstChild() : m_tree->firstChild());
	while (item) {
		count += 1 + basketCount(item);
		item = item->nextSibling();
	}

	return count;
}

bool BNPView::canFold()
{
	BasketListViewItem *item = listViewItemForBasket(currentBasket());
	if (!item)
		return false;
	return item->parent() || (item->firstChild() && item->isOpen());
}

bool BNPView::canExpand()
{
	BasketListViewItem *item = listViewItemForBasket(currentBasket());
	if (!item)
		return false;
	return item->firstChild();
}

BasketListViewItem* BNPView::appendBasket(Basket *basket, QListViewItem *parentItem)
{
	BasketListViewItem *newBasketItem;
	if (parentItem)
		newBasketItem = new BasketListViewItem(parentItem, ((BasketListViewItem*)parentItem)->lastChild(), basket);
	else {
		QListViewItem *child     = m_tree->firstChild();
		QListViewItem *lastChild = 0;
		while (child) {
			lastChild = child;
			child = child->nextSibling();
		}
		newBasketItem = new BasketListViewItem(m_tree, lastChild, basket);
	}

	emit basketNumberChanged(basketCount());

	return newBasketItem;
}

void BNPView::loadNewBasket(const QString &folderName, const QDomElement &properties, Basket *parent)
{
	Basket *basket = loadBasket(folderName);
	appendBasket(basket, (basket ? listViewItemForBasket(parent) : 0));
	basket->loadProperties(properties);
	setCurrentBasket(basket);
//	save();
}

BasketListViewItem* BNPView::lastListViewItem()
{
	QListViewItem *child     = m_tree->firstChild();
	QListViewItem *lastChild = 0;
	// Set lastChild to the last primary child of the list view:
	while (child) {
		lastChild = child;
		child = child->nextSibling();
	}
	// If this child have child(s), recursivly browse through them to find the real last one:
	while (lastChild && lastChild->firstChild()) {
		child = lastChild->firstChild();
		while (child) {
			lastChild = child;
			child = child->nextSibling();
		}
	}
	return (BasketListViewItem*)lastChild;
}

void BNPView::goToPreviousBasket()
{
	if (!m_tree->firstChild())
		return;

	BasketListViewItem *item     = listViewItemForBasket(currentBasket());
	BasketListViewItem *toSwitch = item->shownItemAbove();
	if (!toSwitch) {
		toSwitch = lastListViewItem();
		if (toSwitch && !toSwitch->isShown())
			toSwitch = toSwitch->shownItemAbove();
	}

	if (toSwitch)
		setCurrentBasket(toSwitch->basket());

	if (Settings::usePassivePopup())
		showPassiveContent();
}

void BNPView::goToNextBasket()
{
	if (!m_tree->firstChild())
		return;

	BasketListViewItem *item     = listViewItemForBasket(currentBasket());
	BasketListViewItem *toSwitch = item->shownItemBelow();
	if (!toSwitch)
		toSwitch = ((BasketListViewItem*)m_tree->firstChild());

	if (toSwitch)
		setCurrentBasket(toSwitch->basket());

	if (Settings::usePassivePopup())
		showPassiveContent();
}

void BNPView::foldBasket()
{
	BasketListViewItem *item = listViewItemForBasket(currentBasket());
	if (item && !item->firstChild())
		item->setOpen(false); // If Alt+Left is hitted and there is nothing to close, make sure the focus will go to the parent basket

	QKeyEvent* keyEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Left, 0, 0);
	QApplication::postEvent(m_tree, keyEvent);
}

void BNPView::expandBasket()
{
	QKeyEvent* keyEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Right, 0, 0);
	QApplication::postEvent(m_tree, keyEvent);
}

void BNPView::closeAllEditors()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = (BasketListViewItem*)(it.current());
		item->basket()->closeEditor();
		++it;
	}
}

bool BNPView::convertTexts()
{
	bool convertedNotes = false;
	KProgressDialog dialog(
			/*parent=*/0,
			/*name=*/"",
			/*caption=*/i18n("Plain Text Notes Conversion"),
			/*text=*/i18n("Converting plain text notes to rich text ones..."),
			/*modal=*/true);
	dialog.progressBar()->setTotalSteps(basketCount());
	dialog.show(); //setMinimumDuration(50/*ms*/);

	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = (BasketListViewItem*)(it.current());
		if (item->basket()->convertTexts())
			convertedNotes = true;
		dialog.progressBar()->advance(1);
		if (dialog.wasCancelled())
			break;
		++it;
	}

	return convertedNotes;
}

/** isRunning is to avoid recursive calls because this method can be called
 * when clicking the menu action or when using the filter-bar icon... either of those calls
 * call the other to be checked... and it can cause recursive calls.
 * PS: Uggly hack? Yes, I think so :-)
 */
void BNPView::toggleFilterAllBaskets(bool doFilter)
{
	static bool isRunning = false;
	if (isRunning)
		return;
	isRunning = true;

	// Set the state:
	m_actFilterAllBaskets->setChecked(doFilter);
	//currentBasket()->decoration()->filterBar()->setFilterAll(doFilter);

//	Basket *current = currentBasket();
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		item->basket()->decoration()->filterBar()->setFilterAll(doFilter);
		++it;
	}

	// Protection is not necessary anymore:
	isRunning = false;

	if (doFilter)
		currentBasket()->decoration()->filterBar()->setEditFocus();

	// Filter every baskets:
	newFilter();
}

/** This function can be called recursively because we call kapp->processEvents().
 * If this function is called whereas another "instance" is running,
 * this new "instance" leave and set up a flag that is read by the first "instance"
 * to know it should re-begin the work.
 * PS: Yes, that's a very lame pseudo-threading but that works, and it's programmer-efforts cheap :-)
 */
void BNPView::newFilter()
{
	static bool alreadyEntered = false;
	static bool shouldRestart  = false;

	if (alreadyEntered) {
		shouldRestart = true;
		return;
	}
	alreadyEntered = true;
	shouldRestart  = false;

	Basket *current = currentBasket();
	const FilterData &filterData = current->decoration()->filterBar()->filterData();

	// Set the filter data for every other baskets, or reset the filter for every other baskets if we just disabled the filterInAllBaskets:
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		if (item->basket() != current)
			if (isFilteringAllBaskets())
				item->basket()->decoration()->filterBar()->setFilterData(filterData); // Set the new FilterData for every other baskets
			else
				item->basket()->decoration()->filterBar()->setFilterData(FilterData()); // We just disabled the global filtering: remove the FilterData
		++it;
	}

	// Show/hide the "little filter icons" (during basket load)
	// or the "little numbers" (to show number of found notes in the baskets) is the tree:
	m_tree->triggerUpdate();
	kapp->processEvents();

	// Load every baskets for filtering, if they are not already loaded, and if necessary:
	if (filterData.isFiltering) {
		Basket *current = currentBasket();
		QListViewItemIterator it(m_tree);
		while (it.current()) {
			BasketListViewItem *item = ((BasketListViewItem*)it.current());
			if (item->basket() != current) {
				Basket *basket = item->basket();
				if (!basket->loadingLaunched() && !basket->isLocked())
					basket->load();
				basket->filterAgain();
				m_tree->triggerUpdate();
				kapp->processEvents();
				if (shouldRestart) {
					alreadyEntered = false;
					shouldRestart  = false;
					newFilter();
					return;
				}
			}
			++it;
		}
	}

	m_tree->triggerUpdate();
//	kapp->processEvents();

	alreadyEntered = false;
	shouldRestart  = false;
}

void BNPView::newFilterFromFilterBar()
{
	if (isFilteringAllBaskets())
		QTimer::singleShot(0, this, SLOT(newFilter())); // Keep time for the QLineEdit to display the filtered character and refresh correctly!
}

bool BNPView::isFilteringAllBaskets()
{
	return m_actFilterAllBaskets->isChecked();
}


BasketListViewItem* BNPView::listViewItemForBasket(Basket *basket)
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		if (item->basket() == basket)
			return item;
		++it;
	}
	return 0L;
}

Basket* BNPView::currentBasket()
{
	DecoratedBasket *decoBasket = (DecoratedBasket*)m_stack->visibleWidget();
	if (decoBasket)
		return decoBasket->basket();
	else
		return 0;
}

Basket* BNPView::parentBasketOf(Basket *basket)
{
	BasketListViewItem *item = (BasketListViewItem*)(listViewItemForBasket(basket)->parent());
	if (item)
		return item->basket();
	else
		return 0;
}

void BNPView::setCurrentBasket(Basket *basket)
{
	if (currentBasket() == basket)
		return;

	if (currentBasket())
		currentBasket()->closeBasket();

	BasketListViewItem *item = listViewItemForBasket(basket);
	if (item) {
		m_tree->setSelected(item, true);
		item->ensureVisible();
		m_stack->raiseWidget(basket->decoration());
		// If the window has changed size, only the current basket receive the event,
		// the others will receive ony one just before they are shown.
		// But this triggers unwanted animations, so we eliminate it:
		basket->relayoutNotes(/*animate=*/false);
		basket->openBasket();
		setCaption(item->basket()->basketName());
		countsChanged(basket);
		updateStatusBarHint();
		if (Global::systemTray)
			Global::systemTray->updateToolTip();
		m_tree->ensureItemVisible(m_tree->currentItem());
	}
	m_tree->viewport()->update();
	emit basketChanged();
}

void BNPView::removeBasket(Basket *basket)
{
	if (basket->isDuringEdit())
		basket->closeEditor();

	// Find a new basket to switch to and select it.
	// Strategy: get the next sibling, or the previous one if not found.
	// If there is no such one, get the parent basket:
	BasketListViewItem *basketItem = listViewItemForBasket(basket);
	BasketListViewItem *nextBasketItem = (BasketListViewItem*)(basketItem->nextSibling());
	if (!nextBasketItem)
		nextBasketItem = basketItem->prevSibling();
	if (!nextBasketItem)
		nextBasketItem = (BasketListViewItem*)(basketItem->parent());

	if (nextBasketItem)
		setCurrentBasket(nextBasketItem->basket());

	// Remove from the view:
	basket->unsubscribeBackgroundImages();
	m_stack->removeWidget(basket->decoration());
//	delete basket->decoration();
	delete basketItem;
//	delete basket;

	// If there is no basket anymore, add a new one:
	if (!nextBasketItem)
		BasketFactory::newBasket(/*icon=*/"", /*name=*/i18n("General"), /*backgroundImage=*/"", /*backgroundColor=*/QColor(), /*textColor=*/QColor(), /*templateName=*/"1column", /*createIn=*/0);
	else // No need to save two times if we add a basket
		save();

	emit basketNumberChanged(basketCount());
}

void BNPView::setTreePlacement(bool onLeft)
{
	if (onLeft)
		moveToFirst(m_tree);
	else
		moveToLast(m_tree);
	//updateGeometry();
	kapp->postEvent( this, new QResizeEvent(size(), size()) );
}

void BNPView::relayoutAllBaskets()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		//item->basket()->unbufferizeAll();
		item->basket()->unsetNotesWidth();
		item->basket()->relayoutNotes(true);
		++it;
	}
}

void BNPView::recomputeAllStyles()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		item->basket()->recomputeAllStyles();
		item->basket()->unsetNotesWidth();
		item->basket()->relayoutNotes(true);
		++it;
	}
}

void BNPView::removedStates(const QValueList<State*> &deletedStates)
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		item->basket()->removedStates(deletedStates);
		++it;
	}
}

void BNPView::linkLookChanged()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		item->basket()->linkLookChanged();
		++it;
	}
}

void BNPView::filterPlacementChanged(bool onTop)
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item        = static_cast<BasketListViewItem*>(it.current());
		DecoratedBasket    *decoration  = static_cast<DecoratedBasket*>(item->basket()->parent());
		decoration->setFilterBarPosition(onTop);
		++it;
	}
}

void BNPView::updateBasketListViewItem(Basket *basket)
{
	BasketListViewItem *item = listViewItemForBasket(basket);
	if (item)
		item->setup();

	if (basket == currentBasket()) {
		setCaption(basket->basketName());
		if (Global::systemTray)
			Global::systemTray->updateToolTip();
	}

	// Don't save if we are loading!
	if (!m_loading)
		save();
}

void BNPView::needSave(QListViewItem*)
{
	if (!m_loading)
		// A basket has been collapsed/expanded or a new one is select: this is not urgent:
		QTimer::singleShot(500/*ms*/, this, SLOT(save()));
}

void BNPView::slotPressed(QListViewItem *item, const QPoint &/*pos*/, int /*column*/)
{
	Basket *basket = currentBasket();
	if (basket == 0)
		return;

	// Impossible to Select no Basket:
	if (!item)
		m_tree->setSelected(listViewItemForBasket(basket), true);
	else if (dynamic_cast<BasketListViewItem*>(item) != 0 && currentBasket() != ((BasketListViewItem*)item)->basket()) {
		setCurrentBasket( ((BasketListViewItem*)item)->basket() );
		needSave(0);
	}
	basket->setFocus();
}

DecoratedBasket* BNPView::currentDecoratedBasket()
{
	if (currentBasket())
		return currentBasket()->decoration();
	else
		return 0;
}

// Redirected actions :

void BNPView::exportToHTML()              { HTMLExporter exporter(currentBasket());  }
void BNPView::editNote()                  { currentBasket()->noteEdit();             }
void BNPView::cutNote()                   { currentBasket()->noteCut();              }
void BNPView::copyNote()                  { currentBasket()->noteCopy();             }
void BNPView::delNote()                   { currentBasket()->noteDelete();           }
void BNPView::openNote()                  { currentBasket()->noteOpen();             }
void BNPView::openNoteWith()              { currentBasket()->noteOpenWith();         }
void BNPView::saveNoteAs()                { currentBasket()->noteSaveAs();           }
void BNPView::noteGroup()                 { currentBasket()->noteGroup();            }
void BNPView::noteUngroup()               { currentBasket()->noteUngroup();          }
void BNPView::moveOnTop()                 { currentBasket()->noteMoveOnTop();        }
void BNPView::moveOnBottom()              { currentBasket()->noteMoveOnBottom();     }
void BNPView::moveNoteUp()                { currentBasket()->noteMoveNoteUp();       }
void BNPView::moveNoteDown()              { currentBasket()->noteMoveNoteDown();     }
void BNPView::slotSelectAll()             { currentBasket()->selectAll();            }
void BNPView::slotUnselectAll()           { currentBasket()->unselectAll();          }
void BNPView::slotInvertSelection()       { currentBasket()->invertSelection();      }
void BNPView::slotResetFilter()           { currentDecoratedBasket()->resetFilter(); }

void BNPView::importKJots()       { SoftwareImporters::importKJots();       }
void BNPView::importKNotes()      { SoftwareImporters::importKNotes();      }
void BNPView::importKnowIt()      { SoftwareImporters::importKnowIt();      }
void BNPView::importTuxCards()    { SoftwareImporters::importTuxCards();    }
void BNPView::importStickyNotes() { SoftwareImporters::importStickyNotes(); }
void BNPView::importTomboy()      { SoftwareImporters::importTomboy();      }
void BNPView::importTextFile()    { SoftwareImporters::importTextFile();    }

void BNPView::backupRestore()
{
	BackupDialog dialog;
	dialog.exec();
}

void BNPView::countsChanged(Basket *basket)
{
	if (basket == currentBasket())
		notesStateChanged();
}

void BNPView::notesStateChanged()
{
	Basket *basket = currentBasket();

	// Update statusbar message :
	if (currentBasket()->isLocked())
		setSelectionStatus(i18n("Locked"));
	else if (!basket->isLoaded())
		setSelectionStatus(i18n("Loading..."));
	else if (basket->count() == 0)
		setSelectionStatus(i18n("No notes"));
	else {
		QString count     = i18n("%n note",     "%n notes",    basket->count()         );
		QString selecteds = i18n("%n selected", "%n selected", basket->countSelecteds());
		QString showns    = (currentDecoratedBasket()->filterData().isFiltering ? i18n("all matches") : i18n("no filter"));
		if (basket->countFounds() != basket->count())
			showns = i18n("%n match", "%n matches", basket->countFounds());
		setSelectionStatus(
				i18n("e.g. '18 notes, 10 matches, 5 selected'", "%1, %2, %3").arg(count, showns, selecteds) );
	}

	// If we added a note that match the global filter, update the count number in the tree:
	if (isFilteringAllBaskets())
		listViewItemForBasket(basket)->listView()->triggerUpdate();

	if (currentBasket()->redirectEditActions()) {
		m_actSelectAll         ->setEnabled( !currentBasket()->selectedAllTextInEditor() );
		m_actUnselectAll       ->setEnabled( currentBasket()->hasSelectedTextInEditor()  );
	} else {
		m_actSelectAll         ->setEnabled( basket->countSelecteds() < basket->countFounds() );
		m_actUnselectAll       ->setEnabled( basket->countSelecteds() > 0                     );
	}
	m_actInvertSelection   ->setEnabled( basket->countFounds() > 0 );

	updateNotesActions();
}

void BNPView::updateNotesActions()
{
	bool isLocked             = currentBasket()->isLocked();
	bool oneSelected          = currentBasket()->countSelecteds() == 1;
	bool oneOrSeveralSelected = currentBasket()->countSelecteds() >= 1;
	bool severalSelected      = currentBasket()->countSelecteds() >= 2;

	// FIXME: m_actCheckNotes is also modified in void BNPView::areSelectedNotesCheckedChanged(bool checked)
	//        bool Basket::areSelectedNotesChecked() should return false if bool Basket::showCheckBoxes() is false
//	m_actCheckNotes->setChecked( oneOrSeveralSelected &&
//	                             currentBasket()->areSelectedNotesChecked() &&
//	                             currentBasket()->showCheckBoxes()             );

	Note *selectedGroup = (severalSelected ? currentBasket()->selectedGroup() : 0);

	m_actEditNote            ->setEnabled( !isLocked && oneSelected && !currentBasket()->isDuringEdit() );
	if (currentBasket()->redirectEditActions()) {
		m_actCutNote         ->setEnabled( currentBasket()->hasSelectedTextInEditor() );
		m_actCopyNote        ->setEnabled( currentBasket()->hasSelectedTextInEditor() );
		m_actPaste           ->setEnabled( true                                       );
		m_actDelNote         ->setEnabled( currentBasket()->hasSelectedTextInEditor() );
	} else {
		m_actCutNote         ->setEnabled( !isLocked && oneOrSeveralSelected );
		m_actCopyNote        ->setEnabled(              oneOrSeveralSelected );
		m_actPaste           ->setEnabled( !isLocked                         );
		m_actDelNote         ->setEnabled( !isLocked && oneOrSeveralSelected );
	}
	m_actOpenNote        ->setEnabled(              oneOrSeveralSelected );
	m_actOpenNoteWith    ->setEnabled(              oneSelected          ); // TODO: oneOrSeveralSelected IF SAME TYPE
	m_actSaveNoteAs      ->setEnabled(              oneSelected          ); // IDEM?
	m_actGroup           ->setEnabled( !isLocked && severalSelected && (!selectedGroup || selectedGroup->isColumn()) );
	m_actUngroup         ->setEnabled( !isLocked && selectedGroup && !selectedGroup->isColumn() );
	m_actMoveOnTop       ->setEnabled( !isLocked && oneOrSeveralSelected && !currentBasket()->isFreeLayout() );
	m_actMoveNoteUp      ->setEnabled( !isLocked && oneOrSeveralSelected ); // TODO: Disable when unavailable!
	m_actMoveNoteDown    ->setEnabled( !isLocked && oneOrSeveralSelected );
	m_actMoveOnBottom    ->setEnabled( !isLocked && oneOrSeveralSelected && !currentBasket()->isFreeLayout() );

	for (KAction *action = m_insertActions.first(); action; action = m_insertActions.next())
		action->setEnabled( !isLocked );

	// From the old Note::contextMenuEvent(...) :
/*	if (useFile() || m_type == Link) {
	m_type == Link ? i18n("&Open target")         : i18n("&Open")
	m_type == Link ? i18n("Open target &with...") : i18n("Open &with...")
	m_type == Link ? i18n("&Save target as...")   : i18n("&Save a copy as...")
		// If useFile() theire is always a file to open / open with / save, but :
	if (m_type == Link) {
			if (url().prettyURL().isEmpty() && runCommand().isEmpty())     // no URL nor runCommand :
	popupMenu->setItemEnabled(7, false);                       //  no possible Open !
			if (url().prettyURL().isEmpty())                               // no URL :
	popupMenu->setItemEnabled(8, false);                       //  no possible Open with !
			if (url().prettyURL().isEmpty() || url().path().endsWith("/")) // no URL or target a folder :
	popupMenu->setItemEnabled(9, false);                       //  not possible to save target file
}
} else if (m_type != Color) {
	popupMenu->insertSeparator();
	popupMenu->insertItem( SmallIconSet("filesaveas"), i18n("&Save a copy as..."), this, SLOT(slotSaveAs()), 0, 10 );
}*/
}

// BEGIN Color picker (code from KColorEdit):

/* Activate the mode
 */
void BNPView::slotColorFromScreen(bool global)
{
	m_colorPickWasGlobal = global;
	if (isMainWindowActive()) {
		if(Global::mainWindow()) Global::mainWindow()->hide();
		m_colorPickWasShown = true;
	} else
		m_colorPickWasShown = false;

		currentBasket()->saveInsertionData();
		m_colorPicker->pickColor();

/*	m_gettingColorFromScreen = true;
		kapp->processEvents();
		QTimer::singleShot( 100, this, SLOT(grabColorFromScreen()) );*/
}

void BNPView::slotColorFromScreenGlobal()
{
	slotColorFromScreen(true);
}

void BNPView::colorPicked(const QColor &color)
{
	if (!currentBasket()->isLoaded()) {
		showPassiveLoading(currentBasket());
		currentBasket()->load();
	}
	currentBasket()->insertColor(color);

	if (m_colorPickWasShown)
		showMainWindow();

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Picked color to basket <i>%1</i>"));
}

void BNPView::colorPickingCanceled()
{
	if (m_colorPickWasShown)
		showMainWindow();
}

void BNPView::slotConvertTexts()
{
/*
	int result = KMessageBox::questionYesNoCancel(
		this,
		i18n(
			"<p>This will convert every text notes into rich text notes.<br>"
			"The content of the notes will not change and you will be able to apply formating to those notes.</p>"
			"<p>This process cannot be reverted back: you will not be able to convert the rich text notes to plain text ones later.</p>"
			"<p>As a beta-tester, you are strongly encouraged to do the convert process because it is to test if plain text notes are still needed.<br>"
			"If nobody complain about not having plain text notes anymore, then the final version is likely to not support plain text notes anymore.</p>"
			"<p><b>Which basket notes do you want to convert?</b></p>"
		),
		i18n("Convert Text Notes"),
		KGuiItem(i18n("Only in the Current Basket")),
		KGuiItem(i18n("In Every Baskets"))
	);
	if (result == KMessageBox::Cancel)
		return;
*/

	bool conversionsDone;
//	if (result == KMessageBox::Yes)
//		conversionsDone = currentBasket()->convertTexts();
//	else
		conversionsDone = convertTexts();

	if (conversionsDone)
		KMessageBox::information(this, i18n("The plain text notes have been converted to rich text."), i18n("Conversion Finished"));
	else
		KMessageBox::information(this, i18n("There are no plain text notes to convert."), i18n("Conversion Finished"));
}

QPopupMenu* BNPView::popupMenu(const QString &menuName)
{
	QPopupMenu *menu = 0;
	bool hack = false; // TODO fix this
	// When running in kontact and likeback Information message is shown
	// factory is 0. Don't show error then and don't crash either :-)

	if(m_guiClient)
	{
		KXMLGUIFactory* factory = m_guiClient->factory();
		if(factory)
		{
			menu = (QPopupMenu *)factory->container(menuName, m_guiClient);
		}
		else
			hack = isPart();
	}
	if (menu == 0) {
		if(!hack)
		{
			KStandardDirs stdDirs;
			KMessageBox::error( this, i18n(
					"<p><b>The file basketui.rc seems to not exist or is too old.<br>"
							"%1 cannot run without it and will stop.</b></p>"
							"<p>Please check your installation of %2.</p>"
							"<p>If you do not have administrator access to install the application "
							"system wide, you can copy the file basketui.rc from the installation "
							"archive to the folder <a href='file://%3'>%4</a>.</p>"
							"<p>As last ressort, if you are sure the application is correctly installed "
							"but you had a preview version of it, try to remove the "
							"file %5basketui.rc</p>")
							.arg(kapp->aboutData()->programName(), kapp->aboutData()->programName(),
								stdDirs.saveLocation("data", "basket/")).arg(stdDirs.saveLocation("data", "basket/"), stdDirs.saveLocation("data", "basket/")),
					i18n("Ressource not Found"), KMessageBox::AllowLink );
		}
		if(!isPart())
			exit(1); // We SHOULD exit right now and abord everything because the caller except menu != 0 to not crash.
		else
			menu = new KPopupMenu; // When running in kpart we cannot exit
	}
	return menu;
}

void BNPView::showHideFilterBar(bool show, bool switchFocus)
{
//	if (show != m_actShowFilter->isChecked())
//		m_actShowFilter->setChecked(show);
	m_actShowFilter->setChecked(currentDecoratedBasket()->filterData().isFiltering);

	currentDecoratedBasket()->setFilterBarShown(show, switchFocus);
	currentDecoratedBasket()->resetFilter();
}

void BNPView::insertEmpty(int type)
{
	if (currentBasket()->isLocked()) {
		showPassiveImpossible(i18n("Cannot add note."));
		return;
	}
	currentBasket()->insertEmptyNote(type);
}

void BNPView::insertWizard(int type)
{
	if (currentBasket()->isLocked()) {
		showPassiveImpossible(i18n("Cannot add note."));
		return;
	}
	currentBasket()->insertWizard(type);
}

// BEGIN Screen Grabbing: // FIXME

void BNPView::grabScreenshot(bool global)
{
	if (m_regionGrabber) {
		KWin::activateWindow(m_regionGrabber->winId());
		return;
	}

	// Delay before to take a screenshot because if we hide the main window OR the systray popup menu,
	// we should wait the windows below to be repainted!!!
	// A special case is where the action is triggered with the global keyboard shortcut.
	// In this case, global is true, and we don't wait.
	// In the future, if global is also defined for other cases, check for
	// enum KAction::ActivationReason { UnknownActivation, EmulatedActivation, AccelActivation, PopupMenuActivation, ToolBarActivation };
	int delay = (isMainWindowActive() ? 500 : (global/*kapp->activePopupWidget()*/ ? 0 : 200));

	m_colorPickWasGlobal = global;
	if (isMainWindowActive()) {
		if(Global::mainWindow()) Global::mainWindow()->hide();
		m_colorPickWasShown = true;
	} else
		m_colorPickWasShown = false;

		currentBasket()->saveInsertionData();
		m_regionGrabber = new RegionGrabber(delay);
		connect( m_regionGrabber, SIGNAL(regionGrabbed(const QPixmap&)), this, SLOT(screenshotGrabbed(const QPixmap&)) );
}

void BNPView::grabScreenshotGlobal()
{
	grabScreenshot(true);
}

void BNPView::screenshotGrabbed(const QPixmap &pixmap)
{
	delete m_regionGrabber;
	m_regionGrabber = 0;

	// Cancelled (pressed Escape):
	if (pixmap.isNull()) {
		if (m_colorPickWasShown)
			showMainWindow();
		return;
	}

	if (!currentBasket()->isLoaded()) {
		showPassiveLoading(currentBasket());
		currentBasket()->load();
	}
	currentBasket()->insertImage(pixmap);

	if (m_colorPickWasShown)
		showMainWindow();

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Grabbed screen zone to basket <i>%1</i>"));
}

Basket* BNPView::basketForFolderName(const QString &/*folderName*/)
{
/*	QPtrList<Basket> basketsList = listBaskets();
	Basket *basket;
	for (basket = basketsList.first(); basket; basket = basketsList.next())
	if (basket->folderName() == folderName)
	return basket;
*/
	return 0;
}

void BNPView::setFiltering(bool filtering)
{
	m_actShowFilter->setChecked(filtering);
	m_actResetFilter->setEnabled(filtering);
}

void BNPView::undo()
{
	// TODO
}

void BNPView::redo()
{
	// TODO
}

void BNPView::pasteToBasket(int /*index*/, QClipboard::Mode /*mode*/)
{
	//TODO: REMOVE!
	//basketAt(index)->pasteNote(mode);
}

void BNPView::propBasket()
{
	BasketPropertiesDialog dialog(currentBasket(), this);
	dialog.exec();
}

void BNPView::delBasket()
{
//	DecoratedBasket *decoBasket    = currentDecoratedBasket();
	Basket          *basket        = currentBasket();

#if 0
	KDialogBase *dialog = new KDialogBase(this, /*name=*/0, /*modal=*/true, /*caption=*/i18n("Delete Basket"),
										  KDialogBase::User1 | KDialogBase::User2 | KDialogBase::No, KDialogBase::User1,
										 /*separator=*/false,
										 /*user1=*/KGuiItem(i18n("Delete Only that Basket")/*, icon=""*/),
										 /*user2=*/KGuiItem(i18n("Delete Note & Children")/*, icon=""*/) );
	QStringList basketsList;
	basketsList.append("Basket 1");
	basketsList.append("  Basket 2");
	basketsList.append("    Basket 3");
	basketsList.append("  Basket 4");
	KMessageBox::createKMessageBox(
			dialog, QMessageBox::Information,
			i18n("<qt>Do you really want to remove the basket <b>%1</b> and its contents?</qt>")
				.arg(Tools::textToHTMLWithoutP(basket->basketName())),
			basketsList, /*ask=*/"", /*checkboxReturn=*/0, /*options=*/KMessageBox::Notify/*, const QString &details=QString::null*/);
#endif

	int really = KMessageBox::questionYesNo( this,
											 i18n("<qt>Do you really want to remove the basket <b>%1</b> and its contents?</qt>")
													 .arg(Tools::textToHTMLWithoutP(basket->basketName())),
											 i18n("Remove Basket")
#if KDE_IS_VERSION( 3, 2, 90 ) // KDE 3.3.x
													 , KGuiItem(i18n("&Remove Basket"), "editdelete"), KStdGuiItem::cancel());
#else
		                    );
#endif

	if (really == KMessageBox::No)
		return;

	QStringList basketsList = listViewItemForBasket(basket)->childNamesTree();
	if (basketsList.count() > 0) {
		int deleteChilds = KMessageBox::questionYesNoList( this,
				i18n("<qt><b>%1</b> have the following children baskets.<br>Do you want to remove them too?</qt>")
						.arg(Tools::textToHTMLWithoutP(basket->basketName())),
				basketsList,
				i18n("Remove Children Baskets")
#if KDE_IS_VERSION( 3, 2, 90 ) // KDE 3.3.x
						, KGuiItem(i18n("&Remove Children Baskets"), "editdelete"));
#else
		);
#endif

		if (deleteChilds == KMessageBox::No)
			listViewItemForBasket(basket)->moveChildsBaskets();
	}

	doBasketDeletion(basket);

//	basketNumberChanged();
//	rebuildBasketsMenu();
}

void BNPView::doBasketDeletion(Basket *basket)
{
	basket->closeEditor();

	QListViewItem *basketItem = listViewItemForBasket(basket);
	QListViewItem *nextOne;
	for (QListViewItem *child = basketItem->firstChild(); child; child = nextOne) {
		nextOne = child->nextSibling();
		// First delete the child baskets:
		doBasketDeletion(((BasketListViewItem*)child)->basket());
	}
	// Then, basket have no child anymore, delete it:
	DecoratedBasket *decoBasket = basket->decoration();
	basket->deleteFiles();
	removeBasket(basket);
	// Remove the action to avoir keyboard-shortcut clashes:
	delete basket->m_action; // FIXME: It's quick&dirty. In the future, the Basket should be deleted, and then the KAction deleted in the Basket destructor.
	delete decoBasket;
//	delete basket;
}

void BNPView::password()
{
#ifdef HAVE_LIBGPGME
	PasswordDlg dlg(kapp->activeWindow(), "Password");
	Basket *cur = currentBasket();

	dlg.setType(cur->encryptionType());
	dlg.setKey(cur->encryptionKey());
	if(dlg.exec()) {
		cur->setProtection(dlg.type(), dlg.key());
		if (cur->encryptionType() != Basket::NoEncryption)
			cur->lock();
	}
#endif
}

void BNPView::lockBasket()
{
#ifdef HAVE_LIBGPGME
	Basket *cur = currentBasket();

	cur->lock();
#endif
}

void BNPView::saveAsArchive()
{
	Basket *basket = currentBasket();

	QDir dir;

	KConfig *config = KGlobal::config();
	config->setGroup("Basket Archive");
	QString folder = config->readEntry("lastFolder", QDir::homeDirPath()) + "/";
	QString url = folder + QString(basket->basketName()).replace("/", "_") + ".baskets";

	QString filter = "*.baskets|" + i18n("Basket Archives") + "\n*|" + i18n("All Files");
	QString destination = url;
	for (bool askAgain = true; askAgain; ) {
		destination = KFileDialog::getSaveFileName(destination, filter, this, i18n("Save as Basket Archive"));
		if (destination.isEmpty()) // User canceled
			return;
		if (dir.exists(destination)) {
			int result = KMessageBox::questionYesNoCancel(
				this,
				"<qt>" + i18n("The file <b>%1</b> already exists. Do you really want to override it?")
					.arg(KURL(destination).fileName()),
				i18n("Override File?"),
				KGuiItem(i18n("&Override"), "filesave")
			);
			if (result == KMessageBox::Cancel)
				return;
			else if (result == KMessageBox::Yes)
				askAgain = false;
		} else
			askAgain = false;
	}
	bool withSubBaskets = true;//KMessageBox::questionYesNo(this, i18n("Do you want to export sub-baskets too?"), i18n("Save as Basket Archive")) == KMessageBox::Yes;

	config->writeEntry("lastFolder", KURL(destination).directory());
	config->sync();

	Archive::save(basket, withSubBaskets, destination);
}

QString BNPView::s_fileToOpen = "";

void BNPView::delayedOpenArchive()
{
	Archive::open(s_fileToOpen);
}

void BNPView::openArchive()
{
	QString filter = "*.baskets|" + i18n("Basket Archives") + "\n*|" + i18n("All Files");
	QString path = KFileDialog::getOpenFileName(QString::null, filter, this, i18n("Open Basket Archive"));
	if (!path.isEmpty()) // User has not canceled
		Archive::open(path);
}


void BNPView::activatedTagShortcut()
{
	Tag *tag = Tag::tagForKAction((KAction*)sender());
	currentBasket()->activatedTagShortcut(tag);
}

void BNPView::slotBasketNumberChanged(int number)
{
	m_actPreviousBasket->setEnabled(number > 1);
	m_actNextBasket    ->setEnabled(number > 1);
}

void BNPView::slotBasketChanged()
{
	m_actFoldBasket->setEnabled(canFold());
	m_actExpandBasket->setEnabled(canExpand());
	setFiltering(currentBasket() && currentBasket()->decoration()->filterData().isFiltering);
}

void BNPView::currentBasketChanged()
{
}

void BNPView::isLockedChanged()
{
	bool isLocked = currentBasket()->isLocked();

	setLockStatus(isLocked);

//	m_actLockBasket->setChecked(isLocked);
	m_actPropBasket->setEnabled(!isLocked);
	m_actDelBasket ->setEnabled(!isLocked);
	updateNotesActions();
}

void BNPView::askNewBasket()
{
	askNewBasket(0, 0);
}

void BNPView::askNewBasket(Basket *parent, Basket *pickProperties)
{
	NewBasketDefaultProperties properties;
	if (pickProperties) {
		properties.icon            = pickProperties->icon();
		properties.backgroundImage = pickProperties->backgroundImageName();
		properties.backgroundColor = pickProperties->backgroundColorSetting();
		properties.textColor       = pickProperties->textColorSetting();
		properties.freeLayout      = pickProperties->isFreeLayout();
		properties.columnCount     = pickProperties->columnsCount();
	}

	NewBasketDialog(parent, properties, this).exec();
}

void BNPView::askNewSubBasket()
{
	askNewBasket( /*parent=*/currentBasket(), /*pickPropertiesOf=*/currentBasket() );
}

void BNPView::askNewSiblingBasket()
{
	askNewBasket( /*parent=*/parentBasketOf(currentBasket()), /*pickPropertiesOf=*/currentBasket() );
}

void BNPView::globalPasteInCurrentBasket()
{
	currentBasket()->setInsertPopupMenu();
	pasteInCurrentBasket();
	currentBasket()->cancelInsertPopupMenu();
}

void BNPView::pasteInCurrentBasket()
{
	currentBasket()->pasteNote();

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Clipboard content pasted to basket <i>%1</i>"));
}

void BNPView::pasteSelInCurrentBasket()
{
	currentBasket()->pasteNote(QClipboard::Selection);

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Selection pasted to basket <i>%1</i>"));
}

void BNPView::showPassiveDropped(const QString &title)
{
	if ( ! currentBasket()->isLocked() ) {
		// TODO: Keep basket, so that we show the message only if something was added to a NOT visible basket
		m_passiveDroppedTitle     = title;
		m_passiveDroppedSelection = currentBasket()->selectedNotes();
		QTimer::singleShot( c_delayTooltipTime, this, SLOT(showPassiveDroppedDelayed()) );
		// DELAY IT BELOW:
	} else
		showPassiveImpossible(i18n("No note was added."));
}

void BNPView::showPassiveDroppedDelayed()
{
	if (isMainWindowActive() || m_passiveDroppedSelection == 0)
		return;

	QString title = m_passiveDroppedTitle;

	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::systemTray : this);
	QPixmap contentsPixmap = NoteDrag::feedbackPixmap(m_passiveDroppedSelection);
	QMimeSourceFactory::defaultFactory()->setPixmap("_passivepopup_image_", contentsPixmap);
	m_passivePopup->setView(
			title.arg(Tools::textToHTMLWithoutP(currentBasket()->basketName())),
	(contentsPixmap.isNull() ? "" : "<img src=\"_passivepopup_image_\">"),
	kapp->iconLoader()->loadIcon(currentBasket()->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::showPassiveImpossible(const QString &message)
{
	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::systemTray : (QWidget*)this);
	m_passivePopup->setView(
			QString("<font color=red>%1</font>")
			.arg(i18n("Basket <i>%1</i> is locked"))
			.arg(Tools::textToHTMLWithoutP(currentBasket()->basketName())),
	message,
	kapp->iconLoader()->loadIcon(currentBasket()->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::showPassiveContentForced()
{
	showPassiveContent(/*forceShow=*/true);
}

void BNPView::showPassiveContent(bool forceShow/* = false*/)
{
	if (!forceShow && isMainWindowActive())
		return;

	// FIXME: Duplicate code (2 times)
	QString message;

	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::systemTray : (QWidget*)this);
	m_passivePopup->setView(
			"<qt>" + kapp->makeStdCaption( currentBasket()->isLocked()
			? QString("%1 <font color=gray30>%2</font>")
			.arg(Tools::textToHTMLWithoutP(currentBasket()->basketName()), i18n("(Locked)"))
	: Tools::textToHTMLWithoutP(currentBasket()->basketName()) ),
	message,
	kapp->iconLoader()->loadIcon(currentBasket()->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::showPassiveLoading(Basket *basket)
{
	if (isMainWindowActive())
		return;

	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::systemTray : (QWidget*)this);
	m_passivePopup->setView(
			Tools::textToHTMLWithoutP(basket->basketName()),
	i18n("Loading..."),
	kapp->iconLoader()->loadIcon(basket->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::addNoteText()  { showMainWindow(); currentBasket()->insertEmptyNote(NoteType::Text);  }
void BNPView::addNoteHtml()  { showMainWindow(); currentBasket()->insertEmptyNote(NoteType::Html);  }
void BNPView::addNoteImage() { showMainWindow(); currentBasket()->insertEmptyNote(NoteType::Image); }
void BNPView::addNoteLink()  { showMainWindow(); currentBasket()->insertEmptyNote(NoteType::Link);  }
void BNPView::addNoteColor() { showMainWindow(); currentBasket()->insertEmptyNote(NoteType::Color); }

void BNPView::aboutToHideNewBasketPopup()
{
	QTimer::singleShot(0, this, SLOT(cancelNewBasketPopup()));
}

void BNPView::cancelNewBasketPopup()
{
	m_newBasketPopup = false;
}

void BNPView::setNewBasketPopup()
{
	m_newBasketPopup = true;
}

void BNPView::setCaption(QString s)
{
	emit setWindowCaption(s);
}

void BNPView::updateStatusBarHint()
{
	m_statusbar->updateStatusBarHint();
}

void BNPView::setSelectionStatus(QString s)
{
	m_statusbar->setSelectionStatus(s);
}

void BNPView::setLockStatus(bool isLocked)
{
	m_statusbar->setLockStatus(isLocked);
}

void BNPView::postStatusbarMessage(const QString& msg)
{
	m_statusbar->postStatusbarMessage(msg);
}

void BNPView::setStatusBarHint(const QString &hint)
{
	m_statusbar->setStatusBarHint(hint);
}

void BNPView::setUnsavedStatus(bool isUnsaved)
{
	m_statusbar->setUnsavedStatus(isUnsaved);
}

void BNPView::setActive(bool active)
{
//	std::cout << "Main Window Position: setActive(" << (active ? "true" : "false") << ")" << std::endl;
	KMainWindow* win = Global::mainWindow();
	if(!win)
		return;

#if KDE_IS_VERSION( 3, 2, 90 )   // KDE 3.3.x
	if (active) {
		kapp->updateUserTimestamp(); // If "activate on mouse hovering systray", or "on drag throught systray"
		Global::systemTray->setActive();   //  FIXME: add this in the places it need
	} else
		Global::systemTray->setInactive();
#elif KDE_IS_VERSION( 3, 1, 90 ) // KDE 3.2.x
	// Code from Kopete (that seem to work, in waiting KSystemTray make puplic the toggleSHown) :
	if (active) {
		win->show();
		//raise() and show() should normaly deIconify the window. but it doesn't do here due
		// to a bug in Qt or in KDE  (qt3.1.x or KDE 3.1.x) then, i have to call KWin's method
		if (win->isMinimized())
			KWin::deIconifyWindow(winId());

		if ( ! KWin::windowInfo(winId(), NET::WMDesktop).onAllDesktops() )
			KWin::setOnDesktop(winId(), KWin::currentDesktop());
		win->raise();
		// Code from me: expected and correct behavviour:
		kapp->updateUserTimestamp(); // If "activate on mouse hovering systray", or "on drag throught systray"
		KWin::activateWindow(win->winId());
	} else
		win->hide();
#else                            // KDE 3.1.x and lower
	if (win->active) {
		if (win->isMinimized())
			win->hide();        // If minimized, show() doesn't work !
		win->show();            // Show it
	//		showNormal();      // If it was minimized
		win->raise();           // Raise it on top
		win->setActiveWindow(); // And set it the active window
	} else
		win->hide();
#endif
}

void BNPView::hideOnEscape()
{
	if (Settings::useSystray())
		setActive(false);
}

bool BNPView::isPart()
{
	return (strcmp(name(), "BNPViewPart") == 0);
}

bool BNPView::isMainWindowActive()
{
	KMainWindow* main = Global::mainWindow();
	if (main && main->isActiveWindow())
		return true;
	return false;
}

void BNPView::newBasket()
{
	askNewBasket();
}

void BNPView::handleCommandLine()
{
	KCmdLineArgs *args = KCmdLineArgs::parsedArgs();

	/* Custom data folder */
	QCString customDataFolder = args->getOption("data-folder");
	if (customDataFolder != 0 && !customDataFolder.isEmpty())
	{
		Global::setCustomSavesFolder(customDataFolder);
	}
	/* Debug window */
	if (args->isSet("debug")) {
		new DebugWindow();
		Global::debugWindow->show();
	}

	/* Crash Handler to Mail Developers when Crashing: */
#ifndef BASKET_USE_DRKONQI
	if (!args->isSet("use-drkonquy"))
		KCrash::setCrashHandler(Crash::crashHandler);
#endif
}

/** Scenario of "Hide main window to system tray icon when mouse move out of the window" :
 * - At enterEvent() we stop m_tryHideTimer
 * - After that and before next, we are SURE cursor is hovering window
 * - At leaveEvent() we restart m_tryHideTimer
 * - Every 'x' ms, timeoutTryHide() seek if cursor hover a widget of the application or not
 * - If yes, we musn't hide the window
 * - But if not, we start m_hideTimer to hide main window after a configured elapsed time
 * - timeoutTryHide() continue to be called and if cursor move again to one widget of the app, m_hideTimer is stopped
 * - If after the configured time cursor hasn't go back to a widget of the application, timeoutHide() is called
 * - It then hide the main window to systray icon
 * - When the user will show it, enterEvent() will be called the first time he enter mouse to it
 * - ...
 */

/** Why do as this ? Problems with the use of only enterEvent() and leaveEvent() :
 * - Resize window or hover titlebar isn't possible : leave/enterEvent
 *   are
 *   > Use the grip or Alt+rightDND to resize window
 *   > Use Alt+DND to move window
 * - Each menu trigger the leavEvent
 */

void BNPView::enterEvent(QEvent*)
{
	if(m_tryHideTimer)
		m_tryHideTimer->stop();
	if(m_hideTimer)
		m_hideTimer->stop();
}

void BNPView::leaveEvent(QEvent*)
{
	if (Settings::useSystray() && Settings::hideOnMouseOut() && m_tryHideTimer)
		m_tryHideTimer->start(50);
}

void BNPView::timeoutTryHide()
{
	// If a menu is displayed, do nothing for the moment
	if (kapp->activePopupWidget() != 0L)
		return;

	if (kapp->widgetAt(QCursor::pos()) != 0L)
		m_hideTimer->stop();
	else if ( ! m_hideTimer->isActive() ) // Start only one time
		m_hideTimer->start(Settings::timeToHideOnMouseOut() * 100, true);

	// If a sub-dialog is oppened, we musn't hide the main window:
	if (kapp->activeWindow() != 0L && kapp->activeWindow() != Global::mainWindow())
		m_hideTimer->stop();
}

void BNPView::timeoutHide()
{
	// We check that because the setting can have been set to off
	if (Settings::useSystray() && Settings::hideOnMouseOut())
		setActive(false);
	m_tryHideTimer->stop();
}

void BNPView::changedSelectedNotes()
{
//	tabChanged(0); // FIXME: NOT OPTIMIZED
}

/*void BNPView::areSelectedNotesCheckedChanged(bool checked)
{
	m_actCheckNotes->setChecked(checked && currentBasket()->showCheckBoxes());
}*/

void BNPView::enableActions()
{
	Basket *basket = currentBasket();
	if(!basket)
		return;
	if(m_actLockBasket)
		m_actLockBasket->setEnabled(!basket->isLocked() && basket->isEncrypted());
	if(m_actPassBasket)
		m_actPassBasket->setEnabled(!basket->isLocked());
	m_actPropBasket->setEnabled(!basket->isLocked());
	m_actDelBasket->setEnabled(!basket->isLocked());
	m_actExportToHtml->setEnabled(!basket->isLocked());
	m_actShowFilter->setEnabled(!basket->isLocked());
	m_actFilterAllBaskets->setEnabled(!basket->isLocked());
	m_actResetFilter->setEnabled(!basket->isLocked());
	basket->decoration()->filterBar()->setEnabled(!basket->isLocked());
}

void BNPView::showMainWindow()
{
	KMainWindow *win = Global::mainWindow();

	if (win)
	{
		win->show();
	}
	setActive(true);
	emit showPart();
}

void BNPView::populateTagsMenu()
{
	KPopupMenu *menu = (KPopupMenu*)(popupMenu("tags"));
	if (menu == 0 || currentBasket() == 0) // TODO: Display a messagebox. [menu is 0, surely because on first launch, the XMLGUI does not work!]
		return;
	menu->clear();

	Note *referenceNote;
	if (currentBasket()->focusedNote() && currentBasket()->focusedNote()->isSelected())
		referenceNote = currentBasket()->focusedNote();
	else
		referenceNote = currentBasket()->firstSelected();

	populateTagsMenu(*menu, referenceNote);

	m_lastOpenedTagsMenu = menu;
//	connect( menu, SIGNAL(aboutToHide()), this, SLOT(disconnectTagsMenu()) );
}

void BNPView::populateTagsMenu(KPopupMenu &menu, Note *referenceNote)
{
	if (currentBasket() == 0)
		return;

	currentBasket()->m_tagPopupNote = referenceNote;
	bool enable = currentBasket()->countSelecteds() > 0;

	QValueList<Tag*>::iterator it;
	Tag *currentTag;
	State *currentState;
	int i = 10;
	for (it = Tag::all.begin(); it != Tag::all.end(); ++it) {
		// Current tag and first state of it:
		currentTag = *it;
		currentState = currentTag->states().first();
		QKeySequence sequence;
		if (!currentTag->shortcut().isNull())
			sequence = currentTag->shortcut().operator QKeySequence();
		menu.insertItem(StateMenuItem::checkBoxIconSet(
			(referenceNote ? referenceNote->hasTag(currentTag) : false),
			menu.colorGroup()),
			new StateMenuItem(currentState, sequence, true),
			i
		);
		if (!currentTag->shortcut().isNull())
			menu.setAccel(sequence, i);
		menu.setItemEnabled(i, enable);
		++i;
	}

	menu.insertSeparator();
//	menu.insertItem( /*SmallIconSet("editdelete"),*/ "&Assign new Tag...", 1 );
	//id = menu.insertItem( SmallIconSet("editdelete"), "&Remove All", -2 );
	//if (referenceNote->states().isEmpty())
	//	menu.setItemEnabled(id, false);
//	menu.insertItem( SmallIconSet("configure"),  "&Customize...", 3 );
	menu.insertItem( new IndentedMenuItem(i18n("&Assign new Tag...")),          1 );
	menu.insertItem( new IndentedMenuItem(i18n("&Remove All"),   "editdelete"), 2 );
	menu.insertItem( new IndentedMenuItem(i18n("&Customize..."), "configure"),  3 );

	menu.setItemEnabled(1, enable);
	if (!currentBasket()->selectedNotesHaveTags())
		menu.setItemEnabled(2, false);

	connect( &menu, SIGNAL(activated(int)), currentBasket(), SLOT(toggledTagInMenu(int)) );
	connect( &menu, SIGNAL(aboutToHide()),  currentBasket(), SLOT(unlockHovering())      );
	connect( &menu, SIGNAL(aboutToHide()),  currentBasket(), SLOT(disableNextClick())    );
}

void BNPView::connectTagsMenu()
{
	connect( popupMenu("tags"), SIGNAL(aboutToShow()), this, SLOT(populateTagsMenu())   );
	connect( popupMenu("tags"), SIGNAL(aboutToHide()), this, SLOT(disconnectTagsMenu()) );
}

/*
 * The Tags menu is ONLY created once the BasKet KPart is first shown.
 * So we can use this menu only from then?
 * When the KPart is changed in Kontact, and then the BasKet KPart is shown again,
 * Kontact created a NEW Tags menu. So we should connect again.
 * But when Kontact main window is hidden and then re-shown, the menu does not change.
 * So we disconnect at hide event to ensure only one connection: the next show event will not connects another time.
 */

void BNPView::showEvent(QShowEvent*)
{
	if (isPart())
		QTimer::singleShot( 0, this, SLOT(connectTagsMenu()) );

	if (m_firstShow) {
		m_firstShow = false;
		onFirstShow();
	}
	if (isPart()/*TODO: && !LikeBack::enabledBar()*/) {
		Global::likeBack->enableBar();
	}
}

void BNPView::hideEvent(QHideEvent*)
{
	if (isPart()) {
		disconnect( popupMenu("tags"), SIGNAL(aboutToShow()), this, SLOT(populateTagsMenu())   );
		disconnect( popupMenu("tags"), SIGNAL(aboutToHide()), this, SLOT(disconnectTagsMenu()) );
	}

	if (isPart())
		Global::likeBack->disableBar();
}

void BNPView::disconnectTagsMenu()
{
	QTimer::singleShot( 0, this, SLOT(disconnectTagsMenuDelayed()) );
}

void BNPView::disconnectTagsMenuDelayed()
{
	disconnect( m_lastOpenedTagsMenu, SIGNAL(activated(int)), currentBasket(), SLOT(toggledTagInMenu(int)) );
	disconnect( m_lastOpenedTagsMenu, SIGNAL(aboutToHide()),  currentBasket(), SLOT(unlockHovering())      );
	disconnect( m_lastOpenedTagsMenu, SIGNAL(aboutToHide()),  currentBasket(), SLOT(disableNextClick())    );
}

void BNPView::showGlobalShortcutsSettingsDialog()
{
	KKeyDialog::configure(Global::globalAccel);
	//.setCaption(..)
	Global::globalAccel->writeSettings();
}

#include "bnpview.moc"
