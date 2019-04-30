#ifndef SETENVDLG_H
#define SETENVDLG_H

#include <QtGui/QWidget>

#include <KDialog>

class QString;
class QWidget;

class KLineEdit;

class SetEnvDialog : public KDialog
{
    Q_OBJECT

public:
    SetEnvDialog(QWidget *parent = NULL, const QString& name = QString());
    ~SetEnvDialog();

    const QString& getIncludeSetting();
    const QString& getSourceSetting();
    void setDefaultInclude(const QString&);
    void setDefaultSource(const QString&);
    void setReason(const QString& text) { m_pReason->setText(text); }

private slots:
    void slotApplyClicked();

private:
    KLineEdit *includeEd;
    KLineEdit *sourceEd;

    QLabel *m_pReason;
    QString m_sIncludeDirs;
    QString m_sSourceDirs;
};

#endif

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
