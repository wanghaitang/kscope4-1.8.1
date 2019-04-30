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

#include <math.h>
#include <stdlib.h>
#include <qfile.h>
#include <qpainter.h>
#include <qtooltip.h>
#include <QTextStream>
#include <QGraphicsItem>
#include <QLabel>
#include <QPolygon>
#include <QMenu>
#include <QMouseEvent>

#include <klocale.h>
#include <kmessagebox.h>

#include "graphwidget.h"
#include "graphnode.h"
#include "graphedge.h"
#include "kscopeconfig.h"
#include "queryviewdlg.h"
#include "encoder.h"
#include "progressdlg.h"

const char* GRAPH_DIRS[] = { "TB", "LR", "BT", "RL" };

const char TMP_TMPL[] = "/tmp/kscope_dot.XXXXXX";
#define TMP_TMPL_SIZE	(sizeof(TMP_TMPL) + 1)

/**
 * Provides a menu separator with text.
 * The separator is added with QMenuData::insertItem(QWidget*).
 * @author Elad Lahav
 */
class MenuLabel : public QLabel
{
public:
	/**
	 * Class constructor.
	 * @param	sText	The text to display
	 * @param	pParent	The parent widget
	 */
	MenuLabel(const QString& sText, QWidget* pParent) :
		QLabel(sText, pParent) {
		QAction* pAction;
		QWidgetAction* pWidgetAction;

		// Set the appropriate visual properties
		setFrameShadow(QFrame::Plain);
		setFrameShape(QFrame::StyledPanel);
		setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		setIndent(0);
		pWidgetAction = new QWidgetAction(pParent);
		pWidgetAction->setDefaultWidget(this);
		pAction = qobject_cast<QAction*>(pWidgetAction);
		pParent->addAction(pAction);
	}
};

ArrowInfo GraphWidget::s_ai;

/**
 * Class constructor.
 * @param	pParent	The parent widget
 * @param	szName	The widget's name
 */
GraphWidget::GraphWidget(QWidget* pParent) :
	QGraphicsView(pParent),
	m_progress(this),
	m_dot(this),
	m_dZoom(1.0),
	m_nMaxNodeDegree(10), // will be overriden by CallTreeDlg
	m_nMultiCallNum(0),
	m_pProgressDlg(NULL)					 
{
	// Create a canvas
	setScene(new QGraphicsScene(this));
	scene()->setBackgroundBrush(Config().getColor(KScopeConfig::GraphBack));

	// Create a persistent Cscope process
	m_pCscope = new CscopeFrontend();

	// Add records output by the Cscope process
	connect(m_pCscope, SIGNAL(dataReady(FrontendToken*)), this,
		SLOT(slotDataReady(FrontendToken*)));

	// Display query progress information
	connect(m_pCscope, SIGNAL(progress(int, int)), this,
		SLOT(slotProgress(int, int)));	

	// Draw the graph when the process has finished
	connect(m_pCscope, SIGNAL(finished(uint)), this,
		SLOT(slotFinished(uint)));

	// Show a multi-call node when a query results in too many records
	connect(m_pCscope, SIGNAL(aborted()), this,
		SLOT(slotAborted()));

	// Redraw the graph when Dot exits
	connect(&m_dot, SIGNAL(finished(uint)), this, SLOT(slotDotFinished()));	

	// Create the node popup menu
	m_pNodePopup = new QMenu(this);

	new MenuLabel(i18n("<b>Called Functions</b>"), m_pNodePopup);
	m_pNodePopup->addSeparator();
	m_pNodePopup->addAction(i18n("Show"), this, SLOT(slotShowCalled()));
	m_pNodePopup->addAction(i18n("List/Filter..."), this, SLOT(slotListCalled()));
	m_pNodePopup->addAction(i18n("Hide"), this, SLOT(slotHideCalled()));
	m_pNodePopup->addSeparator();

	new MenuLabel(i18n("<b>Calling Functions</b>"), m_pNodePopup);
	m_pNodePopup->addSeparator();
	m_pNodePopup->addAction(i18n("Show"), this, SLOT(slotShowCalling()));
	m_pNodePopup->addAction(i18n("List/Filter..."), this, SLOT(slotListCalling()));
	m_pNodePopup->addAction(i18n("Hide"), this, SLOT(slotHideCalling()));
	m_pNodePopup->addSeparator();

	new MenuLabel(i18n("<b>This Function</b>"), m_pNodePopup);
	m_pNodePopup->addSeparator();
	m_pNodePopup->addAction(i18n("Find Definition"), this, SLOT(slotFindDef()));
	m_pNodePopup->addAction(i18n("Remove"), this, SLOT(slotRemoveNode()));

	// Create the multi-call node popup menu
	m_pMultiCallPopup = new QMenu(this);
	m_pMultiCallPopup->addAction(i18n("List..."), this, SLOT(slotMultiCallDetails()));
	m_pMultiCallPopup->addSeparator();
	m_pMultiCallPopup->addAction(i18n("Remove"), this, SLOT(slotRemoveNode()));

	// Create the edge menu
	m_pEdgePopup = new QMenu(this);
	m_pEdgePopup->addAction(i18n("Open Call"), this, SLOT(slotOpenCall()));

	setToolTip(QString());
}

/**
 * Class destructor.
 */
GraphWidget::~GraphWidget()
{
	qDeleteAll(m_dictNodes);
	m_dictNodes.clear();
}

/**
 * Overloaded event handler to catch ToolTip event
 * The tooltip's text is shown for edges only and if the pointer is inside the arrow
 * rectangle ( rectangle's edges are excluded ) The event is forwarded to the normal
 * handler if it is not a QEvent::ToolTip event
 * @param	e	The event
 */
bool GraphWidget::event(QEvent *e)
{
	if (e->type() == QEvent::ToolTip) {
		QHelpEvent *hEvent = static_cast<QHelpEvent *>(e);
		QPoint pt = mapFromParent(hEvent->pos());
		QString sTip;
		QRect tipRect;

		sTip = getTip(pt, tipRect);

		// Show tip text only if the pointer is inside the arrow's rectangle
		if ((sTip != QString::null) && tipRect.contains(pt, true)){
			QToolTip::showText(hEvent->globalPos(), sTip);
		} else {
			QToolTip::hideText();
			e->ignore();
		}
		return true;
	}
	return QGraphicsView::event(e);
}

/**
 * Creates a root node for the graph.
 * The root node defines the connected component which is always displayed
 * (all other connected components are removed when they are no longer
 * strongly connected to the root).
 * @param	sFunc	The function name for the root node
 */
void GraphWidget::setRoot(const QString& sFunc)
{
	// Insert a new node to the graph
	addNode(sFunc);
	draw();
}

/**
 * Locates a node by its name and, if one does not exist, creates a new node.
 * @param	sFunc	The name of a function
 * @return	The node corresponding to the given name
 */
GraphNode* GraphWidget::addNode(const QString& sFunc, bool bMultiCall)
{
	GraphNode* pNode;

	// Look for a node with the given name
	if ((pNode = m_dictNodes.value(sFunc)) == NULL) {
		// Node not found, create it
		pNode = new GraphNode(NULL, sFunc, bMultiCall);
		scene()->addItem(pNode);
		m_dictNodes.insert(sFunc, pNode);
	}

	// Return the found/created node
	return pNode;
}

/**
 * Adds a call to the graph.
 * A call is made between two functions, the caller and the callee.
 * @param	data	Contains information on the call
 */
void GraphWidget::addCall(const CallData& data)
{
	GraphNode* pCaller, * pCallee;
	GraphEdge* pEdge;

	// Find the relevant nodes (create new nodes if necessary)
	pCaller = addNode(data.m_sCaller);
	pCallee = addNode(data.m_sCallee);

	// Create a new edge
	pEdge = pCaller->addOutEdge(pCallee);
	pEdge->setCallInfo(data.m_sFile, data.m_sLine, data.m_sText);
}

/**
 * Creates a special node representing multiple calls to/from a function.
 * Such a node is creates when the number of calls to/from a function exceeds
 * a certain number. Thus the graph does not become too cluttered.
 * A multiple call node can be replaced by some/all of the actual calls by
 * using the "Details..." action in the node's popup menu.
 * @param	sFunc	The parent function
 * @param	bCalled	true if the multiple calls are called from that function,
 *					false if they are calling the function
 */
void GraphWidget::addMultiCall(const QString& sFunc, bool bCalled)
{
	QString sMulti;
	GraphNode* pCaller, * pCallee;

	// Create a unique name for the new node.
	// The name is of the form 0XXX, where XXX is a hexadecimal number. 
	// We assume that no function starts with a digit, and that there are no
	// more than 0xfff multi-call nodes in the graph.
	sMulti.sprintf("0%.3x", m_nMultiCallNum);
	m_nMultiCallNum = (m_nMultiCallNum + 1) & 0xfff;

	// Find the relevant nodes (create new nodes if necessary)
	if (bCalled) {
		pCaller = addNode(sFunc);
		pCallee = addNode(sMulti, true);
	}
	else {
		pCaller = addNode(sMulti, true);
		pCallee = addNode(sFunc);
	}

	// Create a new edge
	pCaller->addOutEdge(pCallee);
}

/**
 * Draws the graph on the canvas using the graphviz engine.
 * A new canvas is created, so all items need to be regenerated.
 * TODO: Can we use the same canvas and only reposition existing items?
 */
void GraphWidget::draw()
{
	char szTempFile[TMP_TMPL_SIZE];
	int nFd;
	FILE* pFile;

	// Do nothing if drawing process has already started
	if (m_dot.state() == QProcess::Running)
		return;

	// Apply the zoom factor
	scale(m_dZoom, m_dZoom);

	// Do not draw until the Dot process finishes
	setUpdatesEnabled(false);

	// Open a temporary file
	strcpy(szTempFile, TMP_TMPL);
	nFd = mkstemp(szTempFile);
	if ((pFile = fdopen(nFd, "w")) == NULL)
		return;

	// Remember the file name (so it can be deleted later)
	m_sDrawFilePath = szTempFile;

	// Write the graph contents to the temporary file
	{
		QTextStream str(pFile, QIODevice::WriteOnly);
		write(str, "graph", "--", false);
	}

	// Close the file
	fclose(pFile);

	// Draw the graph
	if (m_dot.run(szTempFile)) {
		// Create the progress dialogue
		m_pProgressDlg = new ProgressDlg(i18n("KScope"), 
			i18n("Generating graph, please wait"), this);
		m_pProgressDlg->setMinimumDuration(1000);
		m_pProgressDlg->setValue(0);

		// TODO:
		// Implement cancel (what do we do when the drawing process is 
		// terminated, even though the nodes and edges were already added by
		// Cscope?)
		// m_pProgressDlg->setAllowCancel(true);
	}
}

/**
 * Stores a graph on a file.
 * The file uses the dot language to describe the graph.
 * @param	pFile	An open file to write to
 */
void GraphWidget::save(FILE* pFile)
{
	// Write the graph using the dot language
	QTextStream str(pFile, QIODevice::WriteOnly);
	write(str, "digraph", "->", true);
}

/**
 * Exports a graph to a dot file.
 * @param	sFile	The full path of the file to export to
 */
void GraphWidget::save(const QString& sFile)
{
	QFile file(sFile);

	// Open/create the file
	if (!file.open(QIODevice::WriteOnly))
		return;

	QTextStream str(&file);
	write(str, "digraph", "->", false);
}

/**
 * Changes the zoom factor.
 * @param	bIn	true to zoom in, false to zoom out
 */
void GraphWidget::zoom(bool bIn)
{
	// Set the new zoom factor
	if (bIn)
		m_dZoom *= 1.5;
	else
		m_dZoom /= 1.5;

	// Apply the transformation matrix
	scale(m_dZoom, m_dZoom);
}

/**
 * Determines the initial zoom factor.
 * This method is called from the file parser and therefore does not redraw
 * the widget.
 * @param	dZoom	The zoom factor to use
 */
void GraphWidget::setZoom(double dZoom)
{
	m_dZoom = dZoom;
}

/**
 * Changes the graph's direction 90 degrees counter-clockwise.
 */
void GraphWidget::rotate()
{
	QString sDir;
	int i;

	// Get the current direction
	sDir = Config().getGraphOrientation();

	// Find the next direction
	for (i = 0; i < 4 && sDir != GRAPH_DIRS[i]; i++){}
	if (i == 4)
		i = 0;
	else
		i = (i + 1) % 4;

	// Set the new direction
	sDir = GRAPH_DIRS[i];
	Config().setGraphOrientation(sDir);
}

/**
 * Checks if a tool tip is required for the given position.
 * NOTE: We currently return a tool tip for edges only
 * @param	ptPos	The position to query
 * @param	rc		Holds the tip's rectangle, upon return
 * @return	The tip's text, or QString::null if no tip is required
 */
QString GraphWidget::getTip(const QPoint& ptPos, QRect& rc)
{
	QPoint ptRealPos, ptTopLeft, ptBottomRight;
	GraphEdge* pEdge;
	QString sText, sFile, sLine;
	QGraphicsItem *item;

	ptRealPos = mapFromScene(ptPos);
	ptRealPos /= m_dZoom;
	pEdge = NULL;

	// Check if there is an edge at this position
	item = scene()->itemAt(ptRealPos);
	pEdge = dynamic_cast<GraphEdge*>(item);

	// No tip if no edge was found
	if (pEdge == NULL)
		return QString::null;

	// Set the rectangle for the tip (the tip is closed when the mouse leaves
	// this area)
	rc = pEdge->tipRect();
	ptTopLeft = rc.topLeft();
	ptBottomRight = rc.bottomRight();
	ptTopLeft *= m_dZoom;	
	ptBottomRight *= m_dZoom;
	ptTopLeft = mapFromScene(ptTopLeft);
	ptBottomRight = mapFromScene(ptBottomRight);
	rc = QRect(ptTopLeft, ptBottomRight);

	// Create a tip for this edge
	return pEdge->getTip();
}

/**
 * Resizes the canvas.
 * @param	nWidth	The new width
 * @param	nHeight	The new height
 */
void GraphWidget::resize(int nWidth, int nHeight)
{
	scene()->setSceneRect(0, 0, nWidth + 2, nHeight + 2);
}

/**
 * Displays a node on the canvas.
 * Sets the parameters used for drawing the node on the canvas.
 * @param	sFunc	The function corresponding to the node to draw
 * @param	rect	The coordinates of the node's rectangle
 */
void GraphWidget::drawNode(const QString& sFunc, const QRect& rect)
{
	GraphNode* pNode;

	// Find the node
	pNode = addNode(sFunc);

	// Set the visual aspects of the node
	pNode->setRect(rect);
	pNode->setZValue(2.0);
	pNode->setPen(QPen(Qt::black));
	pNode->setFont(Config().getFont(KScopeConfig::Graph));

	if (pNode->isMultiCall())
		pNode->setBrush(Config().getColor(KScopeConfig::GraphMultiCall));
	else
		pNode->setBrush(Config().getColor(KScopeConfig::GraphNode));
}

/**
 * Displays an edge on the canvas.
 * Sets the parameters used for drawing the edge on the canvas.
 * @param	sCaller		Identifies the edge's head node
 * @param	sCallee		Identifies the edge's tail node
 * @param	arrCurve	Control points for the edge's spline
 */
void GraphWidget::drawEdge(const QString& sCaller, const QString& sCallee,
	const QPolygon& arrCurve)
{
	GraphNode* pCaller, * pCallee;
	GraphEdge* pEdge;

	// Find the edge
	pCaller = addNode(sCaller);
	pCallee = addNode(sCallee);
	pEdge = pCaller->addOutEdge(pCallee);

	// Set the visual aspects of the edge
	pEdge->setPoints(arrCurve, s_ai);
	pEdge->setZValue(1.0);
	pEdge->setPen(QPen(Qt::black));
	pEdge->setBrush(QBrush(Qt::black));
}

#define PI 3.14159265

/**
 * Sets and computes values used for drawing arrows.
 * Initialises the static ArroInfo structure, which is passed in drawEdge().
 * @param	nLength		The arrow head length
 * @param	nDegrees	The angle, in degrees, between the base line and each
 *						of the arrow head's sides
 */
void GraphWidget::setArrowInfo(int nLength, int nDegrees)
{
	double dRad;

	// Turn degrees into radians
	dRad = ((double)nDegrees) * PI / 180.0;

	s_ai.m_dLength = (double)nLength;
	s_ai.m_dTan = tan(dRad);
	s_ai.m_dSqrt = sqrt(1 + s_ai.m_dTan * s_ai.m_dTan);
}

/**
 * Draws the contents of the canvas on this view.
 * NOTE: This method is overriden to fix a strange bug in Qt that leaves
 * a border around the canvas part of the view. It should be deleted once
 * this bug is fixed.
 * TODO: Is there a better way of erasing the border?
 * @param	pPainter	Used to paint on the view
 * @param	rect		The rectangle of the area to draw
 */
void GraphWidget::drawForeground(QPainter *pPainter, const QRectF &rect)
{
	// Draw the contents of the canvas
	QGraphicsView::drawForeground(pPainter, rect);

	// Erase the canvas's area border
	if (scene() != NULL) {
		QRectF rect = scene()->sceneRect();
		pPainter->setBrush(QBrush()); // Null brush
		pPainter->setPen(Config().getColor(KScopeConfig::GraphBack));
		pPainter->drawRect(-1, -1, rect.width() + 2, rect.height() + 2);
	}
}

/**
 * Handles mouse clicks over the graph view.
 * @param	pEvent	Includes information on the mouse press event
 */
void GraphWidget::mousePressEvent(QMouseEvent* pEvent)
{
	QPointF ptRealPos;
	QString sFunc;
	GraphNode* pNode;
	GraphEdge* pEdge;
	QGraphicsItem *item;

	pNode = NULL;
	pEdge = NULL;

	// Handle right-clicks only
	if (pEvent->button() != Qt::RightButton) {
		QGraphicsView::mousePressEvent(pEvent);
		return;
	}

	// Take the zoom factor into consideration
	ptRealPos = mapToScene(pEvent->pos());
	ptRealPos /= m_dZoom;

	// Check if an item was clicked
	item = scene()->itemAt(ptRealPos);
	if (dynamic_cast<GraphNode*>(item) != NULL)
		pNode = dynamic_cast<GraphNode*>(item);
	else if (dynamic_cast<GraphEdge*>(item) != NULL)
		pEdge = dynamic_cast<GraphEdge*>(item);

	// Handle clicks over different types of items
	if (pNode != NULL) {
		// Show a context menu for nodes
		showNodeMenu(pNode, pEvent->globalPos());
	}
	else if (pEdge != NULL) {
		// Show a context menu for edges
		showEdgeMenu(pEdge, pEvent->globalPos());
	}
	else {
		// Take the default action
		QGraphicsView::mouseMoveEvent(pEvent);
	}
}

/**
 * Writes a description of the graph to the given stream, using the Dot 
 * language.
 * The method allows for both directed graphs and non-directed graphs, the
 * latter are required for drawing purposes (since Dot will not produce the
 * arrow heads and the splines terminate before reaching the nodes).
 * @param	str			The stream to write to
 * @param	sType		Either "graph" or "digraph"
 * @param	sEdge		The edge connector ("--" or "->")
 * @param	bWriteCall	true to write call information, false otherwise
 */
void GraphWidget::write(QTextStream& str, const QString& sType, 
	const QString& sEdge, bool bWriteCall)
{
	QFont font;
	QHash<QString, GraphNode*>::const_iterator itr;
	const GraphEdge* pEdge;
	Encoder enc;

	font = Config().getFont(KScopeConfig::Graph);

	// Header
	str << sType << " G {\n";

	// Graph attributes
	str << "\tgraph [rankdir=" << Config().getGraphOrientation() << ", "
		<< "kscope_zoom=" << m_dZoom 
		<< "];\n";

	// Default node attributes
	str << "\tnode [shape=box, height=\"0.01\", style=filled, "
		<< "fillcolor=\"" << Config().getColor(KScopeConfig::GraphNode).name()
		<< "\", "
		<< "fontcolor=\"" << Config().getColor(KScopeConfig::GraphText).name()
		<< "\", "
		<< "fontname=\"" << font.family() << "\", "
		<< "fontsize=" << QString::number(font.pointSize())
		<< "];\n";

	// Iterate over all nodes
	for (itr = m_dictNodes.constBegin(); itr != m_dictNodes.constEnd(); ++itr) {
		// Write a node
		str << "\t" << itr.value()->getFunc() << ";\n";

		// Iterate over all edges leaving this node
		const QHash<QString, GraphEdge*> OutEdges(itr.value()->getOutEdges());
		QHash<QString, GraphEdge*>::const_iterator itrEdge;
		for (itrEdge = OutEdges.constBegin(); itrEdge != OutEdges.constEnd(); ++itrEdge) {
			pEdge = itrEdge.value();
			str << "\t" << pEdge->getHead()->getFunc() << sEdge
				<< pEdge->getTail()->getFunc();

			// Write call information
			if (bWriteCall) {
				str << " ["
					<< "kscope_file=\"" << pEdge->getFile() << "\","
					<< "kscope_line=" << pEdge->getLine() << ","
					<< "kscope_text=\"" << enc.encode(pEdge->getText()) << "\"" 
					<< "]";
			}

			str << ";\n";
		}
	}

	// Close the graph
	str << "}\n";
}

/**
 * Removes all edges attached to a function node at the given direction.
 * Any strongly connected components that are no longer connected to that
 * function are deleted.
 * @param	pNode	The node for which to remove the edges
 * @param	bOut	true for outgoing edges, false for incoming
 */
void GraphWidget::removeEdges(GraphNode* pNode, bool bOut)
{
	// Remove the edges
	if (bOut)
		pNode->removeOutEdges();
	else
		pNode->removeInEdges();

	// Remove all graph components no longer connected to this one
	removeDisconnected(pNode);
}

/**
 * Removes all edges and nodes that are not weakly connected to the given node.
 * This function is called to clean up the graph after edges were removed from
 * the given node.
 * @param	pNode	The node to which all other nodes have to be connected
 */
void GraphWidget::removeDisconnected(GraphNode* pNode)
{
	QMutableHashIterator<QString, GraphNode*> itr(m_dictNodes);

	// Find all weakly connected components attached to this node
	pNode->dfs();

	itr = m_dictNodes;
	while (itr.hasNext()) {
		itr.next();
		GraphNode* pDelNode = itr.value();

		if (!pDelNode->dfsVisited()) {
			itr.remove();
			delete pDelNode;
		}
		else {
			pDelNode->dfsReset();
// 			itr.next();
		}
	}
}

/**
 * Shows a popup menu for a node.
 * This menu is shown after a node has been right-clicked.
 * @param	pNode	The node for which to show the menu
 * @param	ptPos	The position of the menu 
 */
void GraphWidget::showNodeMenu(GraphNode* pNode, const QPoint& ptPos)
{
	// Remember the node
	m_pMenuItem = pNode;

	// Show the popup menu.
	if (pNode->isMultiCall())
		m_pMultiCallPopup->popup(ptPos);
	else
		m_pNodePopup->popup(ptPos);
}

/**
 * Shows a popup menu for an edge.
 * This menu is shown after an edge has been right-clicked.
 * @param	pEdge	The edge for which to show the menu
 * @param	ptPos	The position of the menu 
 */
void GraphWidget::showEdgeMenu(GraphEdge* pEdge, const QPoint& ptPos)
{
	// Remember the edge
	m_pMenuItem = pEdge;

	// Show the popup menu.
	m_pEdgePopup->popup(ptPos);
}

/**
 * Redraws the widget when new instructions are available.
 * This slot is connected to the finished() signal emitted by the dot front-
 * end.
 */
void GraphWidget::slotDotFinished()
{
	// Delete the temporary file
	if (m_sDrawFilePath != "") {
		QFile::remove(m_sDrawFilePath);
		m_sDrawFilePath = "";
	}

	// Delete the progress dialogue
	if (m_pProgressDlg) {
		delete m_pProgressDlg;
		m_pProgressDlg = NULL;
	}

	setUpdatesEnabled(true);
	scene()->update();
}

/**
 * Adds an entry to the tree, as the child of the active item.
 * Called by a CscopeFrontend object, when a new entry was received in its
 * whole from the Cscope back-end process. The entry contains the data of a
 * function calling the function described by the active item.
 * @param	pToken	The first token in the entry
 */
void GraphWidget::slotDataReady(FrontendToken* pToken)
{
	CallData data;
	QString sFunc;

	// Get the file name
	data.m_sFile = pToken->getData();
	pToken = pToken->getNext();

	// Get the function name
	sFunc = pToken->getData();
	pToken = pToken->getNext();

	// Get the line number (do not accept global information on a call tree)
	data.m_sLine = pToken->getData();
	if (data.m_sLine.toUInt() == 0)
		return;

	pToken = pToken->getNext();

	// Get the line's text
	data.m_sText = pToken->getData();

	// Determine the caller and the callee
	if (m_bCalled) {
		data.m_sCaller = m_sQueriedFunc;
		data.m_sCallee = sFunc;
	}
	else {
		data.m_sCaller = sFunc;
		data.m_sCallee = m_sQueriedFunc;
	}

	// Add the call to the graph
	addCall(data);
}

/**
 * Displays search progress information.
 * This slot is connected to the progress() signal emitted by a
 * CscopeFrontend object.
 * @param	nProgress	The current progress value
 * @param	nTotal		The expected final value
 */
void GraphWidget::slotProgress(int nProgress, int nTotal)
{
	m_progress.setProgress(nProgress, nTotal);
}

/**
 * Disables the expandability feature of an item, if no functions calling it
 * were found.
 * @param	nRecords	Number of records reported by the query
 */
void GraphWidget::slotFinished(uint /*nRecords*/)
{
	// Destroy the progress bar
	m_progress.finished();

	// Redraw the graph
	draw();
}

/**
 * Adds a multiple call node when a query results in too many entries.
 * This slot is attached to the aborted() signal of the Cscope process.
 */
void GraphWidget::slotAborted()
{	
	KMessageBox::information(this, i18n("The query produced too many results.\n"
		"A multiple-call node will appear in the graph instead.\n"
		"Hint: The maximum number of in/out edges\n"
		"can be adjusted by clicking the dialogue's \"Preferences\" button"));

	addMultiCall(m_sQueriedFunc, m_bCalled);
	draw();
}

/**
 * Shows functions called from the current function node.
 * This slot is connected to the "Show Called Functions" popup menu
 * action.
 */
void GraphWidget::slotShowCalled()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	// Run a query for called functions
	m_sQueriedFunc = pNode->getFunc();
	m_bCalled = true;
	m_pCscope->query(CscopeFrontend::Called, m_sQueriedFunc,
		Config().getGraphMaxNodeDegree());
}

/**
 * Shows a list of function calls from the current node.
 * The list is displayed in a query dialogue. The user can the select which
 * calls should be displayed in the graph.
 * This slot is connected to the "List Called Functions" popup menu
 * action.
 */
void GraphWidget::slotListCalled()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	QueryViewDlg dlg(0, (QWidget*)parent());

	// Show the query view dialogue
	dlg.query(CscopeFrontend::Called, pNode->getFunc());
	if (dlg.exec() != QDialog::Accepted)
		return;

	// The OK button was clicked, replace current calls with the listed ones
	pNode->removeOutEdges();

	QTreeWidgetItemIterator itr(dlg.m_pView, QTreeWidgetItemIterator::NotHidden);
	CallData data;

	data.m_sCaller = pNode->getFunc();

	// Add all listed calls
	while (*itr){
		data.m_sCallee = (*itr)->text(0);
		data.m_sFile = (*itr)->text(1);
		data.m_sLine = (*itr)->text(2);
		data.m_sText = (*itr)->text(3);
		itr++;
		addCall(data);
	}

	// Clean-up and redraw the graph
	removeDisconnected(pNode);
	draw();
}

/**
 * Hides functions called from the current function node.
 * This slot is connected to the "Hide Called Functions" popup menu
 * action.
 */
void GraphWidget::slotHideCalled()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	// Remove edges and redraw the graph
	removeEdges(pNode, true);
	draw();
}

/**
 * Shows functions calling tothe current function node.
 * This slot is connected to the "Show Calling Functions" popup menu
 * action.
 */
void GraphWidget::slotShowCalling()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	// Run a query for called functions
	m_sQueriedFunc = pNode->getFunc();
	m_bCalled = false;
	m_pCscope->query(CscopeFrontend::Calling, m_sQueriedFunc, 
		Config().getGraphMaxNodeDegree());
}

/**
 * Shows a list of function calls to the current node.
 * The list is displayed in a query dialogue. The user can the select which
 * calls should be displayed in the graph.
 * This slot is connected to the "List Calling Functions" popup menu
 * action.
 */
void GraphWidget::slotListCalling()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	QueryViewDlg dlg(0, (QWidget*)parent());

	// Show the query view dialogue
	dlg.query(CscopeFrontend::Calling, pNode->getFunc());
	if (dlg.exec() != QDialog::Accepted)
		return;

	// The OK button was clicked, replace current calls with the listed ones
	pNode->removeInEdges();

	//	QueryView::Iterator itr;
	QTreeWidgetItemIterator itr(dlg.m_pView, QTreeWidgetItemIterator::NotHidden);
	CallData data;

	data.m_sCallee = pNode->getFunc();

	// Add all listed calls
	while (*itr){
		data.m_sCallee = (*itr)->text(0);
		data.m_sFile = (*itr)->text(1);
		data.m_sLine = (*itr)->text(2);
		data.m_sText = (*itr)->text(3);
		itr++;
		addCall(data);
	}

	// Clean-up and redraw the graph
	removeDisconnected(pNode);
	draw();
}

/**
 * Hides functions calling to the current function node.
 * This slot is connected to the "Hide CallingFunctions" popup menu
 * action.
 */
void GraphWidget::slotHideCalling()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	// Remove edges and redraw the graph
	removeEdges(pNode, false);
	draw();
}

/**
 * Looks up the definition of the current function node.
 * This slot is connected to the "Find Definition" popup menu action.
 */
void GraphWidget::slotFindDef()
{
	GraphNode* pNode;
	QueryViewDlg* pDlg;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	// Create a query view dialogue
	pDlg = new QueryViewDlg(QueryViewDlg::DestroyOnSelect, this);

	// Display a line when it is selected in the dialogue
	connect(pDlg, SIGNAL(lineRequested(const QString&, uint)), this,
		SIGNAL(lineRequested(const QString&, uint)));

	// Start the query
	pDlg->query(CscopeFrontend::Definition, pNode->getFunc());
}

/**
 * Deletes a node from the graph (alogn with all edges connected to this 
 * node).
 * The node removed is the one over which the context menu was invoked.
 * This slot is connected to the "Remove" popup menu action.
 */
void GraphWidget::slotRemoveNode()
{
	GraphNode* pNode;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL)
		return;

	// Remove all incoming edges
	pNode->removeInEdges();

	// Remove the node (also deletes the object and its outgoing edges)
	delete m_dictNodes.take(pNode->getFunc());

	// Redraw the graph
	draw();
}

/**
 * Shows the list of calls that is represented by a single multi-call node.
 * This slot handles the "Details..." command of the multi-call node menu.
 */
void GraphWidget::slotMultiCallDetails()
{
	GraphNode* pNode, * pParent;
	bool bCalled;

	// Make sure the menu item is a node
	pNode = dynamic_cast<GraphNode*>(m_pMenuItem);
	if (pNode == NULL || !pNode->isMultiCall())
		return;

	// Get the required information from the node
	pNode->getFirstNeighbour(pParent, bCalled);
	if (!pParent)
		return;

	QueryViewDlg dlg(0, (QWidget*)parent());

	dlg.query(bCalled ? CscopeFrontend::Calling : CscopeFrontend::Called, 
		pParent->getFunc());
	dlg.exec();
}

/**
 * Emits a signal to open an editor at the file and line matching the call
 * information of the current edge.
 * This slot is connected to the "Open Call" popup menu action (for edges).
 */
void GraphWidget::slotOpenCall()
{
	GraphEdge* pEdge;
	QString sFile, sLine;

	// Make sure the menu item is an edge
	pEdge = dynamic_cast<GraphEdge*>(m_pMenuItem);
	if (pEdge != NULL && pEdge->getFile() != "")
		emit lineRequested(pEdge->getFile(), pEdge->getLine());
}

#include "graphwidget.moc"
