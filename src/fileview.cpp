/***************************************************************************
 *
 * Copyright (C) 2005 Elad Lahav (elad_lahav@users.sourceforge.net)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#include <QContextMenuEvent>
#include <QDockWidget>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QTabWidget>

#include <kfiledialog.h>
#include <ktoggleaction.h>

#include "fileview.h"
#include "filelist.h"
#include "kscopeconfig.h"
#include "kscopepixmaps.h"

/**
 * Class constructor.
 * @param	pParent	The parent widget
 * @param	szName	The widget's name
 * @param	fl		Widget creation flags
 */
FileView::FileView(QWidget* pParent, const char* szName, Qt::WFlags fl) :
	QWidget(pParent, fl),
	m_sRoot(""),
	m_pCurrentPath(0)
{
	QWidget* pPage;

	setupUi(this);
	setObjectName(szName);

	// Set the tab widget icons
	pPage = m_pTabWidget->widget(0);
	m_pTabWidget->setTabIcon(m_pTabWidget->indexOf(pPage), GET_PIXMAP(TabFileList));
	pPage = m_pTabWidget->widget(1);
	m_pTabWidget->setTabIcon(m_pTabWidget->indexOf(pPage), GET_PIXMAP(TabFileTree));

	// Setup the default current tab at startup
	m_pTabWidget->setCurrentIndex(Config().getActiveFileWindowTab());
	connect(m_pTabWidget, SIGNAL(currentChanged(int)),
		this, SLOT(slotActiveTabChanged(int)));

	// Setup default curent path of tree view
	m_pCurrentPath = new KUrl(QDir::currentPath());

	// Setup tree view widget
	m_pModel = m_pFileTree->model();
	m_pFileTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_pFileTree->setRootUrl(*m_pCurrentPath);
	m_pFileTree->setCurrentUrl(*m_pCurrentPath);
	m_pFileTree->setSortingEnabled(true);
	m_pFileTree->sortByColumn(0, (Qt::SortOrder)(Config().getFileTreeSortOrder()));
	m_pFileTree->setShowHiddenFiles(Config().getShowHiddenFiles());
	m_pFileTree->setContextMenuPolicy(Qt::CustomContextMenu);

	connect(m_pFileTree->header(), SIGNAL(sortIndicatorChanged(int, Qt::SortOrder)),
		this, SLOT(slotSortOrderChanged(int, Qt::SortOrder)));
	connect(m_pFileTree, SIGNAL(customContextMenuRequested(const QPoint&)),
		this, SLOT(slotContextMenuRequest(const QPoint&)));

	// Hide all but columns <name>, <size> & <type>
	for (int i = KDirModel::Size; i < KDirModel::ColumnCount; m_pFileTree->hideColumn(i++)){}
	m_pFileTree->showColumn(KDirModel::Size);
	m_pFileTree->showColumn(KDirModel::Type);

	// Send the fileRequested() signal whenever a file is selected in either
	// the list or the tree
	connect(m_pFileList, SIGNAL(fileRequested(const QString&, uint)), this,
		SIGNAL(fileRequested(const QString&, uint)));
	connect(m_pFileTree, SIGNAL(activated(const KUrl&)), 
		this, SLOT(slotTreeItemSelected(const KUrl&)));
}

/**
 * Class destructor.
 */
FileView::~FileView()
{
}

/**
 * Sets a new common root path to both the file list and the tree.
 * @param	sRoot	The full path of the new root
 */
void FileView::setRoot(const QString& sRoot)
{
	// Nothing to do if the given root is the same as the old one
	if (sRoot == m_sRoot)
		return;

	m_sRoot = sRoot;

	// Remove the current branch & update new one
	if (m_pCurrentPath)
		delete(m_pCurrentPath);
	m_pCurrentPath = new KUrl(m_sRoot);

	// Update the file list
	m_pFileList->setRoot(m_sRoot);

	// Nothing more to do for an empty root directory
	if (sRoot.isEmpty())
		return;

	// Create and open a new branch, with the newly specified root
	m_pFileTree->setRootUrl(*m_pCurrentPath);
	m_pFileTree->setCurrentUrl(*m_pCurrentPath);
}

/**
 * Clears the contents of the file view and file tree.
 */
void FileView::clear()
{
	m_pFileList->clear();
	setRoot("");
} 

/**
 * Emits the fileRequested() signal when a file name is selected in the file 
 * tree. An item is selected by either double-clicking it or by hittin 
 * "ENTER" when it is highlighted.
 * This slot is connected to the doubleClicked() and returnPressed() signals
 * of the KFileTreeView object.
 * @param	pItem	The selected tree item
 */
void FileView::slotTreeItemSelected(const KUrl& url)
{
	QFileInfo fileInfo = QFileInfo(url.pathOrUrl());

	if (! fileInfo.isDir())
		emit fileRequested(url.pathOrUrl(), 0);
}

/**
 * Activated when the current tab changed. The last current tab index is saved in
 * the global config file and restored when starting a new session
 */
void FileView::slotActiveTabChanged(int index)
{
	Config().setActiveFileWindowTab(index);
}

/**
 * Activated when the sort order changed. The last setting is saved in the global config
 * file and restored when starting a new session. This is valid only for the first
 * column
 */
void FileView::slotSortOrderChanged(int index, Qt::SortOrder newOrder)
{
	if (index == 0) {
		Config().setFileTreeSortOrder((int)newOrder);
	}
}

/**
 * Display a `custom context menu' to show hidden files & folder (or not). The last
 * setting is saved in the global config file. This override the default context menu
 * provided by KFileTreeView
 */
void FileView::slotContextMenuRequest(const QPoint& pt)
{
	QMenu menu;
	KToggleAction *showHiddenAction = new KToggleAction(i18n("Show Hidden Folders"), &menu);
	showHiddenAction->setChecked(Config().getShowHiddenFiles());
	connect(showHiddenAction, SIGNAL(toggled(bool)),
		this, SLOT(slotShowHiddenFiles(bool)));
	menu.addAction(showHiddenAction);
	menu.exec(m_pFileTree->mapToGlobal(pt));
}

void FileView::slotShowHiddenFiles(bool enabled)
{
	m_pFileTree->setShowHiddenFiles(enabled);
	Config().setShowHiddenFiles(enabled);
}

#include "fileview.moc"

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
