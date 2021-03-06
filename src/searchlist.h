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

#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include <qwidget.h>
#include <QtGui/QVBoxLayout>
#include <QtGui/QLineEdit>
#include <QtGui/QTreeWidget>
#include <QtGui/QToolTip>
#include <QKeyEvent>

class SearchList;

/**
 * Defines a line text edit for searchable list view.
 * The widget is based on QLineEdit with additional key functions
 * Supported key events (up and down) are emitted as signals
 * @author Albert Yosher
 */
class SearchLineEdit : public QLineEdit
{
   Q_OBJECT
public:
	SearchLineEdit(QWidget* pParent = 0) : QLineEdit(pParent) {};
	~SearchLineEdit() {};

signals:
	/**
	 * Emitted when one of the up/down or page up/page down keys were pressed
	 * inside this edit widget.
	 * @param	pEvent	The event received for this key press
	 */
	void keyPressed(QKeyEvent* pEvent);

private:
	virtual void keyPressEvent(QKeyEvent*);
};

/**
 * Defines a searchable list view.
 * The widget is composed of a list view, and an edit box used to enter
 * search data. Whenever the text in the edit box changes, the list view is
 * set to point to the first item that matches the new text.
 * @author Elad Lahav
 */
class SearchList : public QWidget
{
   Q_OBJECT

public: 
	SearchList(int nSearchCol, QWidget* pParent = 0, const char* szName = 0);
	~SearchList();

	/**
	 * @return	A pointer to the list part of the widget.
	 */
	QTreeWidget* getList() { return m_pList; }

	/**
	 * Constructs a tool-tip for the given item.
	 * @param	pItem	The item for which a tip is required
	 * @param	sTip	The constructed tip string (on return)
	 * @return	True to display the tip, false otherwise
	 */
	virtual bool getTip(QTreeWidgetItem* pItem, QString& sTip) = 0;

public slots:
	void slotSetFocus();

protected:
	/** The search edit-box. */
	SearchLineEdit* m_pEdit;

	/** The list part of the widget. */
	QTreeWidget* m_pList;

	bool event(QEvent *event);

	/**
	 * Called whenever the user selects an item in the list by either double-
	 * clicking it, or by highlighting the item and pressing the ENTER key.
	 * @param	pItem	The selected list item
	 */
	virtual void processItemSelected(QTreeWidgetItem* pItem) = 0;

protected slots:
	void slotFindItem(const QString&);
	void slotItemSelected(QTreeWidgetItem*, int);
	void slotItemSelected();
	void slotKeyPressed(QKeyEvent*);

private:
	/** Specifies the search column, i.e., the list column whose strings are
		compared with the text in the search edit-box. */
	int m_nSearchCol;

	/** A tool-tip for the list entries. */
	QString* m_pToolTip;
};

#endif
