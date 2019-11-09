/*
 * Copyright (C) 2016 ~ 2018 Deepin Technology Co., Ltd.
 *               2016 ~ 2018 dragondjf
 *
 * Author:     dragondjf<dingjiangfeng@deepin.com>
 *
 * Maintainer: dragondjf<dingjiangfeng@deepin.com>
 *             zccrs<zhangjide@deepin.com>
 *             Tangtong<tangtong@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "propertydialog.h"
#include "dabstractfilewatcher.h"
#include "dfileinfo.h"

#include "app/define.h"

#include "dfileservices.h"
#include "dfilestatisticsjob.h"

#include "shutil/fileutils.h"
#include "shutil/mimesappsmanager.h"
#include "shutil/fileutils.h"

#include "dialogs/dialogmanager.h"
#include "app/filesignalmanager.h"

#include "models/dfmrootfileinfo.h"
#include "deviceinfo/udisklistener.h"

#include "utils.h"

#include "singleton.h"

#include "shareinfoframe.h"
#include "views/dfilemanagerwindow.h"
#include "views/windowmanager.h"
#include "views/dfileview.h"
#include "interfaces/dfilesystemmodel.h"

#include "plugins/pluginmanager.h"
#include "../plugininterfaces/menu/menuinterface.h"
#include "dfmeventdispatcher.h"
#include "views/dfmsidebar.h"

#include <darrowlineexpand.h>
#include <dexpandgroup.h>
#include <dblockdevice.h>
#include <denhancedwidget.h>
#include <QScrollBar>

#include <QTextEdit>
#include <QFormLayout>
#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QTimer>
#include <QThread>
#include <QListWidget>
#include <QButtonGroup>
#include <QProgressBar>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QVariantAnimation>
#include <QScrollArea>
#include <ddiskmanager.h>
#include <QGuiApplication>
#include "unistd.h"
#include <models/trashfileinfo.h>
#include <views/dfmtagwidget.h>

#define ArrowLineExpand_HIGHT   30
#define ArrowLineExpand_SPACING 10

bool DFMRoundBackground::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Paint) {
        QWidget *w = dynamic_cast<QWidget *>(watched);
        if(!w) {
            return false;
        }
        int radius = property("radius").toInt();

        QPainter painter(w);
        QRectF bgRect;
        bgRect.setSize(w->size());
        const QPalette pal = QGuiApplication::palette();
        QColor bgColor = pal.color(QPalette::Background);

        QPainterPath path;
        path.addRoundedRect(bgRect, radius, radius);
        // drawbackground color
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillPath(path, bgColor);
        painter.setRenderHint(QPainter::Antialiasing, false);
        return true;
    }

    return QObject::eventFilter(watched, event);
}


class DFMDArrowLineExpand : public DArrowLineExpand{
public:
    DFMDArrowLineExpand(){
        if (headerLine()) {
            QFont f = headerLine()->font();
            f.setBold(true);
            f.setPixelSize(17);
            headerLine()->setFont(f);
        }
        new DFMRoundBackground(this, 8);
    }
};

class DFProgressBar : public QProgressBar
{
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        QRectF bgRect;
        QRectF fgRect;
        qreal p = 1. * (value() - minimum()) / (maximum() - minimum());

        bgRect.setSize(size());
        fgRect.setSize(QSizeF(width() * p, height()));
        const QPalette pal = this->palette();
        QColor bgColor = pal.color(QPalette::AlternateBase); // or maybe just QPalette::Base ?
        QColor fgColor = pal.color(QPalette::Highlight);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.fillRect(bgRect, bgColor);
        painter.fillRect(fgRect, fgColor);
    }
};

NameTextEdit::NameTextEdit(const QString &text, QWidget *parent):
    QTextEdit(text, parent)
{
    setObjectName("NameTextEdit");
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setFixedSize(200, 60);
    setContextMenuPolicy(Qt::NoContextMenu);

    connect(this, &QTextEdit::textChanged, this, [this] {
        QSignalBlocker blocker(this);
        Q_UNUSED(blocker)

        QString text = this->toPlainText();
        const QString old_text = text;

        int text_length = text.length();

        text.remove('/');
        text.remove(QChar(0));

        QVector<uint> list = text.toUcs4();
        int cursor_pos = this->textCursor().position() - text_length + text.length();

        while (text.toUtf8().size() > MAX_FILE_NAME_CHAR_COUNT)
        {
            list.removeAt(--cursor_pos);

            text = QString::fromUcs4(list.data(), list.size());
        }

        if (text.count() != old_text.count())
        {
            this->setText(text);
        }

        QTextCursor cursor = this->textCursor();

        cursor.movePosition(QTextCursor::Start);

        do
        {
            QTextBlockFormat format = cursor.blockFormat();

            format.setLineHeight(TEXT_LINE_HEIGHT, QTextBlockFormat::FixedHeight);
            cursor.setBlockFormat(format);
        } while (cursor.movePosition(QTextCursor::NextBlock));

        cursor.setPosition(cursor_pos);

        this->setTextCursor(cursor);
        this->setAlignment(Qt::AlignHCenter);

        if (this->isReadOnly())
            this->setFixedHeight(this->document()->size().height());
    });
}

void NameTextEdit::setPlainText(const QString &text)
{
    QTextEdit::setPlainText(text);
    setAlignment(Qt::AlignCenter);
}

void NameTextEdit::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);
}

void NameTextEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        setIsCanceled(true);
        emit editFinished();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        setIsCanceled(false);
        emit editFinished();
    }
    QTextEdit::keyPressEvent(event);
}

bool NameTextEdit::isCanceled() const
{
    return m_isCanceled;
}

void NameTextEdit::setIsCanceled(bool isCanceled)
{
    m_isCanceled = isCanceled;
}


GroupTitleLabel::GroupTitleLabel(const QString &text, QWidget *parent, Qt::WindowFlags f):
    QLabel(text, parent, f)
{
    setObjectName("GroupTitleLabel");
}

SectionKeyLabel::SectionKeyLabel(const QString &text, QWidget *parent, Qt::WindowFlags f):
    QLabel(text, parent, f)
{
    setObjectName("SectionKeyLabel");
    setFixedWidth(120);
    QFont font = this->font();
    font.setWeight(QFont::Bold-8);
    font.setPixelSize(13);
    setFont(font);
    setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
}


SectionValueLabel::SectionValueLabel(const QString &text, QWidget *parent, Qt::WindowFlags f):
    QLabel(text, parent, f)
{
    setObjectName("SectionValueLabel");
    setFixedWidth(150);
    QFont font = this->font();
    font.setPixelSize(12);
    setFont(font);
    setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    setWordWrap(true);
}

LinkSectionValueLabel::LinkSectionValueLabel(const QString &text, QWidget *parent, Qt::WindowFlags f):
    SectionValueLabel(text, parent, f)
{

}

void LinkSectionValueLabel::mouseReleaseEvent(QMouseEvent *event)
{
    DFMEventDispatcher::instance()->processEvent<DFMOpenFileLocation>(Q_NULLPTR, linkTargetUrl());
    SectionValueLabel::mouseReleaseEvent(event);
}

DUrl LinkSectionValueLabel::linkTargetUrl() const
{
    return m_linkTargetUrl;
}

void LinkSectionValueLabel::setLinkTargetUrl(const DUrl &linkTargetUrl)
{
    m_linkTargetUrl = linkTargetUrl;
}


PropertyDialog::PropertyDialog(const DFMEvent &event, const DUrl url, QWidget *parent)
    : DDialog(parent)
    , m_fmevent(event)
    , m_url(url)
    , m_icon(new QLabel)
    , m_edit(new NameTextEdit)
{
    setSizeGripEnabled(true);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags()
                   & ~ Qt::WindowMaximizeButtonHint
                   & ~ Qt::WindowMinimizeButtonHint
                   & ~ Qt::WindowSystemMenuHint);
    QString basicInfo = tr("Basic info");
    QString openWith = tr("Open with");
    QString shareManager = tr("Sharing");
    QString authManager = tr("Permissions");
    initUI();
    QString query = m_url.query();

    if (m_url.scheme() == DFMROOT_SCHEME) {
        DAbstractFileInfoPointer fi = fileService->createFileInfo(this, m_url);
        Q_ASSERT(fi);

        QString name = fi->fileDisplayName();
        QIcon icon = QIcon::fromTheme(fi->iconName());

        m_icon->setPixmap(icon.pixmap(128, 128));
        m_edit->setPlainText(name);
        m_editDisbaled = true;
        const QList<QPair<QString, QString> > & properties = createLocalDeviceInfoWidget(fi);
        m_deviceInfoFrame = createInfoFrame(properties);

        QStringList titleList;
        titleList << basicInfo;
        m_expandGroup = addExpandWidget(titleList);
        m_expandGroup.at(0)->setContent(m_deviceInfoFrame);
        m_expandGroup.at(0)->setExpand(true);

        uint64_t dskspace = fi->extraProperties()["fsSize"].toULongLong();
        uint64_t dskinuse = fi->extraProperties()["fsUsed"].toULongLong();
        QString devid(fi->suffix() == SUFFIX_GVFSMP ? fi->fileDisplayName() : fi->baseName());
        if (devid == name) {
            devid.clear();
        }

        QProgressBar* progbdf = new DFProgressBar();
        progbdf->setMaximum(10000);
        progbdf->setValue((int)(10000. * dskinuse / dskspace));
        progbdf->setMaximumHeight(2);

        QString text = tr("%1 (%2)").arg(name).arg(devid);
        QLabel* lbdf_l = new SectionKeyLabel();
        text = lbdf_l->fontMetrics().elidedText(text, Qt::ElideMiddle, 150);
        lbdf_l->setText(text);
        QLabel* lbdf_r = new SectionKeyLabel(tr("%1 / %2").arg(FileUtils::formatSize(dskinuse)).arg(FileUtils::formatSize(dskspace)));
        lbdf_l->setAlignment(Qt::AlignLeft);
        lbdf_r->setAlignment(Qt::AlignRight);
        lbdf_l->setMaximumWidth(QWIDGETSIZE_MAX);
        lbdf_r->setMaximumWidth(QWIDGETSIZE_MAX);
        QWidget* wdfl = new QWidget();
        wdfl->setLayout(new QHBoxLayout);
        wdfl->layout()->setMargin(0);
        wdfl->layout()->addWidget(lbdf_l);
        wdfl->layout()->addWidget(lbdf_r);

        m_wdf = new QWidget(this);
        m_wdf->setLayout(new QVBoxLayout);
        m_wdf->layout()->setMargin(0);
        m_wdf->layout()->addWidget(wdfl);
        m_wdf->layout()->addWidget(progbdf);
        m_mainLayout->addWidget(m_wdf);

    } else {
        // tagged file basicinfo not complete??
        DUrl realUrl = m_url.isTaggedFile() ? DUrl::fromLocalFile(m_url.fragment()) : m_url;
        const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, realUrl);
        if (!fileInfo) {
            close();
            return;
        }
        m_icon->setPixmap(fileInfo->fileIcon().pixmap(128, 128));
        m_edit->setPlainText(fileInfo->fileDisplayName());
        m_edit->setAlignment(Qt::AlignHCenter);

        if (!fileInfo->canRename()) {
            m_editDisbaled = true;
        }

        m_basicInfoFrame = createBasicInfoWidget(fileInfo);

        QStringList titleList;
        if (fileInfo->isFile()) {
            titleList << basicInfo;
            if (!m_url.isTrashFile()) {
                titleList << openWith;
                titleList << authManager;
            }
        } else {
            titleList << basicInfo;
            if (fileInfo->canShare()) {
                titleList << shareManager;
            }
            if (!fileInfo->isVirtualEntry() && !m_url.isTrashFile()) {
                titleList << authManager;
            }
        }
        m_expandGroup = addExpandWidget(titleList);
        m_expandGroup.at(0)->setContent(m_basicInfoFrame);

        if (fileInfo->isDir()) {
            if (fileInfo->canShare()) {
                m_shareinfoFrame = createShareInfoFrame(fileInfo);
                m_expandGroup.at(1)->setContent(m_shareinfoFrame);
                m_expandGroup.at(1)->setExpand(false);
            }

            if (fileInfo->toLocalFile().isEmpty()) {
                startComputerFolderSize(m_url);
            } else if (fileInfo->isSymLink()) {
                startComputerFolderSize(fileInfo->redirectedFileUrl());
            } else {
                startComputerFolderSize(DUrl::fromLocalFile(fileInfo->toLocalFile()));
            }

            m_fileCount = fileInfo->filesCount();
        } else {
            m_fileCount = 1;
            m_size = fileInfo->size();

            int openWithIndex = titleList.indexOf(openWith);
            if (openWithIndex != -1) {
                m_OpenWithListWidget = createOpenWithListWidget(fileInfo);
                m_expandGroup.at(openWithIndex)->setContent(m_OpenWithListWidget);
                m_expandGroup.at(openWithIndex)->setExpand(false);
            }
        }

        int authMgrIndex = titleList.indexOf(authManager);
        if (authMgrIndex != -1) {
            m_authorityManagementFrame = createAuthorityManagementWidget(fileInfo);
            m_expandGroup.at(authMgrIndex)->setContent(m_authorityManagementFrame);
            m_expandGroup.at(authMgrIndex)->setExpand(false);
        }
    }
    initTextShowFrame(m_edit->toPlainText());
    if (m_editDisbaled) {
        m_editButton->hide();
    }

    loadPluginExpandWidgets();

    m_expandGroup.first()->setExpand(true);
    m_expandGroup.last()->setExpandedSeparatorVisible(false);

    initConnect();
}

void PropertyDialog::initUI()
{
    m_icon->setFixedHeight(128);
    m_icon->setParent(this);
    QFrame *m_editFrame = new QFrame;

    QHBoxLayout *editLayout = new QHBoxLayout;
    editLayout->addStretch();
    editLayout->addWidget(m_edit);
    editLayout->addStretch();
    editLayout->setSpacing(0);
    editLayout->setContentsMargins(0, 0, 0, 0);
    m_editFrame->setLayout(editLayout);
    m_edit->setParent(m_editFrame);

    m_editStackWidget = new QStackedWidget(this);
    m_editStackWidget->addWidget(m_editFrame);

    m_mainLayout = new QVBoxLayout;

    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setMargin(0);
    m_mainLayout->setSpacing(0);
    m_mainLayout->addWidget(m_icon, 0, Qt::AlignHCenter | Qt::AlignTop);
    m_mainLayout->addWidget(m_editStackWidget, 0, Qt::AlignHCenter | Qt::AlignTop);

    QFrame *frame = new QFrame(this);
    frame->setLayout(m_mainLayout);
    addContent(frame);

    m_scrollArea = new QScrollArea();
    QPalette palette = m_scrollArea->viewport()->palette();
    palette.setBrush(QPalette::Background, Qt::NoBrush);
    m_scrollArea->viewport()->setPalette(palette);
    m_scrollArea->setFrameShape(QFrame::Shape::NoFrame);
    QFrame *infoframe= new QFrame;
    QVBoxLayout *scrollWidgetLayout = new QVBoxLayout;
    scrollWidgetLayout->setContentsMargins(15, 0, 15, 0);
    scrollWidgetLayout->setSpacing(ArrowLineExpand_SPACING);
    infoframe->setLayout(scrollWidgetLayout);
    m_scrollArea->setWidget(infoframe);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);

    QVBoxLayout *scrolllayout = new QVBoxLayout;
    scrolllayout->addWidget(m_scrollArea);
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(scrolllayout, 1);

    const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, m_url);
    if (fileInfo && fileInfo->canTag()) {
        DFMTagWidget *tagInfoFrame = new DFMTagWidget(m_url, this);
        new DFMRoundBackground(tagInfoFrame, 8);
        m_tagInfoFrame = tagInfoFrame;

        QFont font = tagInfoFrame->tagTitle()->font();
        font.setBold(true);
        font.setPixelSize(17);
        tagInfoFrame->tagTitle()->setFont(font);
        m_tagInfoFrame->setMaximumHeight(150);
        scrollWidgetLayout->addWidget(m_tagInfoFrame);
    }

    setFixedWidth(350);
}

void PropertyDialog::initConnect()
{
    connect(m_edit, &NameTextEdit::editFinished, this, &PropertyDialog::showTextShowFrame);

    DAbstractFileWatcher *fileWatcher = DFileService::instance()->createFileWatcher(this, m_url);

    connect(fileWatcher, &DAbstractFileWatcher::fileDeleted, this, &PropertyDialog::onChildrenRemoved);
    connect(fileWatcher, &DAbstractFileWatcher::fileMoved, this, [this](const DUrl & from, const DUrl & to) {
        Q_UNUSED(to)

        onChildrenRemoved(from);
    });
}


void PropertyDialog::updateFolderSize(qint64 size)
{
    m_size = size;
    m_fileCount = m_sizeWorker->filesCount() + m_sizeWorker->directorysCount(false);
    m_folderSizeLabel->setText(FileUtils::formatSize(size));
    m_containSizeLabel->setText(QString::number(m_fileCount));
}

void PropertyDialog::renameFile()
{
    const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, m_url);
    m_edit->setPlainText(fileInfo->fileNameOfRename());
    m_editStackWidget->setCurrentIndex(0);
    m_edit->setFixedHeight(m_textShowFrame->height());

    const DAbstractFileInfoPointer pfile = fileService->createFileInfo(this, m_url);
    int endPos = -1;
    if (pfile->isFile()) {

        QString suffixStr{ pfile->suffix() };
        if (suffixStr.isEmpty() == true) {
            endPos = m_edit->toPlainText().length() - pfile->suffix().length();

        } else {
            endPos = m_edit->toPlainText().length() - pfile->suffix().length() - 1;
        }

    }
    if (endPos == -1) {
        m_edit->selectAll();
        endPos = m_edit->toPlainText().length();
    }
    QTextCursor cursor = m_edit->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(endPos, QTextCursor::KeepAnchor);
    m_edit->setTextCursor(cursor);

}

void PropertyDialog::showTextShowFrame()
{
    const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, m_url);

    QString newName = m_edit->toPlainText();
    if (newName.trimmed().isEmpty()) {
        m_edit->setIsCanceled(true);
    }

    if (m_edit->isCanceled()) {
        initTextShowFrame(fileInfo->fileDisplayName());
    } else {
        DUrl oldUrl = m_url;
        DUrl newUrl = fileInfo->getUrlByNewFileName(newName);

        if (oldUrl == newUrl) {
            m_editStackWidget->setCurrentIndex(1);
            return;
        }

        if (fileService->renameFile(this, oldUrl, newUrl)) {
            if (!fileInfo->isDesktopFile()) { // this is a dirty fix.
                m_url = newUrl;
            }
            const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, m_url);

            initTextShowFrame(fileInfo->fileDisplayName());
            dialogManager->refreshPropertyDialogs(oldUrl, newUrl);
            if (m_shareinfoFrame) {
                m_shareinfoFrame->setFileinfo(fileInfo);
            }
        } else {
            m_editStackWidget->setCurrentIndex(1);
        }
    }
}

void PropertyDialog::onChildrenRemoved(const DUrl &fileUrl)
{
    if (m_url.isUserShareFile()) {
        return;
    }
    if (fileUrl == m_url) {
        close();
    }
}

void PropertyDialog::flickFolderToSidebar()
{

    DFileManagerWindow *window = qobject_cast<DFileManagerWindow *>(WindowManager::getWindowById(m_fmevent.windowId()));
    if (!window) {
        return;
    }

    //when current window is minimized,cancle animation
    if (window->windowState() == Qt::WindowMinimized) {
        return;
    }

    // we are actually using network group's center position as target position
    // since it's used as a animation target position, a fuzzy result is okay.
    DFMSideBar *sideBar = window->getLeftSideBar();
    QString groupName = DFMSideBar::groupName(DFMSideBar::GroupName::Network);
    sideBar->scrollToGroup(groupName);
    QPoint targetPos = sideBar->groupGeometry(groupName).center();

    const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, m_url);

    QLabel *aniLabel = new QLabel(window);
    aniLabel->raise();
    aniLabel->setFixedSize(m_icon->size());
    aniLabel->setAttribute(Qt::WA_TranslucentBackground);
    aniLabel->setPixmap(fileInfo->fileIcon().pixmap(160, 160));
    aniLabel->move(window->mapFromGlobal(m_icon->mapToGlobal(m_icon->pos())));

    int angle;
    if (targetPos.x() > aniLabel->x()) {
        angle = 45;
    } else {
        angle = -45;
    }

    QVariantAnimation *xani = new QVariantAnimation(this);
    xani->setStartValue(aniLabel->pos());
    xani->setEndValue(QPoint(targetPos.x(), angle));
    xani->setDuration(440);

    QVariantAnimation *gani = new QVariantAnimation(this);
    gani->setStartValue(aniLabel->geometry());
    gani->setEndValue(QRect(targetPos.x(), targetPos.y(), 20, 20));
    gani->setEasingCurve(QEasingCurve::InBack);
    gani->setDuration(440);

    connect(xani, &QVariantAnimation::valueChanged, [ = ](const QVariant & val) {
        if (aniLabel) {
            aniLabel->move(QPoint(val.toPoint().x() - aniLabel->width() / 2, aniLabel->y()));
            QImage img = fileInfo->fileIcon().pixmap(aniLabel->size()).toImage();
            QMatrix ma;
            ma.rotate(val.toPoint().y());
            img = img.transformed(ma, Qt::SmoothTransformation);
            img = img.scaled(aniLabel->width() / 2, aniLabel->height() / 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            aniLabel->setPixmap(QPixmap::fromImage(img));
            if (aniLabel->isHidden()) {
                aniLabel->show();
            }
        }
    });
    connect(xani, &QVariantAnimation::finished, [ = ] {
        xani->deleteLater();
    });

    connect(gani, &QVariantAnimation::valueChanged, [ = ](const QVariant & val) {
        aniLabel->move(QPoint(aniLabel->x(),
                              val.toRect().y() - val.toRect().width() / 2));
        aniLabel->setFixedSize(val.toRect().size() * 2);
    });
    connect(gani, &QVariantAnimation::finished, [ = ] {
        gani->deleteLater();
        aniLabel->deleteLater();
    });
    xani->start();
    gani->start();
}
void PropertyDialog::onOpenWithBntsChecked(QAbstractButton *w)
{
    if (w) {
        MimesAppsManager::setDefautlAppForTypeByGio(w->property("mimeTypeName").toString(),
                w->property("appPath").toString());
    }
}

void PropertyDialog::onExpandChanged(const bool &e)
{
    DArrowLineExpand *expand = qobject_cast<DArrowLineExpand *>(sender());
    if (expand) {
        if (e) {
            expand->setSeparatorVisible(false);
        } else {
            QTimer::singleShot(200, expand, [ = ] {
                expand->setSeparatorVisible(true);
            });
        }
    }
}

void PropertyDialog::mousePressEvent(QMouseEvent *event)
{
    if (m_edit->isVisible()) {
        if (event->button() != Qt::RightButton) {
            m_edit->setIsCanceled(m_edit->toPlainText().isEmpty());
            emit m_edit->editFinished();
        }
    }
    DDialog::mousePressEvent(event);
}

void PropertyDialog::startComputerFolderSize(const DUrl &url)
{
    DUrl validUrl = url;
    if (url.isUserShareFile()) {
        validUrl.setScheme(FILE_SCHEME);
    }
    DUrlList urls;
    urls << validUrl;

    if (!m_sizeWorker)
        m_sizeWorker = new DFileStatisticsJob(this);

    connect(m_sizeWorker, &DFileStatisticsJob::dataNotify, this, &PropertyDialog::updateFolderSize);

    m_sizeWorker->start(urls);
}

void PropertyDialog::toggleFileExecutable(bool isChecked)
{
    DAbstractFileInfoPointer info = DFileService::instance()->createFileInfo(this, m_url);
    if (isChecked) {
        DFileService::instance()->setPermissions(this, m_url, info->permissions() | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
    } else {
        DFileService::instance()->setPermissions(this, m_url, info->permissions() & ~(QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther));
    }
}

DUrl PropertyDialog::getUrl()
{
    return m_url;
}

int PropertyDialog::getFileCount()
{
    return m_fileCount;
}

qint64 PropertyDialog::getFileSize()
{
    return m_size;
}

void PropertyDialog::raise()
{
    DDialog::raise();
    emit raised();
}

void PropertyDialog::hideEvent(QHideEvent *event)
{
    emit aboutToClosed(m_url);
    DDialog::hideEvent(event);
    emit closed(m_url);
    if (m_sizeWorker) {
        m_sizeWorker->stop();
    }
}

void PropertyDialog::resizeEvent(QResizeEvent *event)
{
    DDialog::resizeEvent(event);
}

const QList<DBaseExpand *> &PropertyDialog::expandGroup() const
{
    return m_expandGroup;
}

int PropertyDialog::contentHeight() const
{
    int expandsHeight = ArrowLineExpand_SPACING;
    for (const DBaseExpand* expand : m_expandGroup) {
        expandsHeight += expand->height();
    }
#define DIALOG_TITLEBAR_HEIGHT 50
    return (DIALOG_TITLEBAR_HEIGHT +
            m_icon->height() +
            m_editStackWidget->height() +
            expandsHeight +
            contentsMargins().top() +
            contentsMargins().bottom() +
            (m_wdf ? m_wdf->height() : 0)+
            (m_tagInfoFrame ? m_tagInfoFrame->height() : 0)+
            40);
}

void PropertyDialog::loadPluginExpandWidgets()
{
    //QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(this->layout());
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(m_scrollArea->widget()->layout());
    QList<PropertyDialogExpandInfoInterface *> plugins = PluginManager::instance()->getExpandInfoInterfaces();
    foreach (PropertyDialogExpandInfoInterface *plugin, plugins) {
        DFMDArrowLineExpand *expand = new DFMDArrowLineExpand;//DArrowLineExpand;
        QWidget *frame = plugin->expandWidget(m_url.toString());
        if (!frame) {
            expand->deleteLater();
            continue;
        }
        frame->setMaximumHeight(EXTEND_FRAME_MAXHEIGHT);
        frame->setParent(this);
        expand->setTitle(plugin->expandWidgetTitle(m_url.toString()));
        expand->setExpand(false);
        expand->setContent(frame);

        initExpand(layout, expand);
        m_expandGroup.push_back(expand);
    }
    layout->addStretch();
}

void PropertyDialog::initExpand(QVBoxLayout *layout, DBaseExpand *expand)
{
    expand->setFixedHeight(ArrowLineExpand_HIGHT);
    QMargins cm = layout->contentsMargins();
    QRect rc = contentsRect();
    expand->setFixedWidth(rc.width()-cm.left()-cm.right());
    layout->addWidget(expand, 0, Qt::AlignTop);

    connect(expand, &DBaseExpand::expandChange, this, &PropertyDialog::onExpandChanged);
    DEnhancedWidget *hanceedWidget = new DEnhancedWidget(expand, expand);
    connect(hanceedWidget, &DEnhancedWidget::heightChanged, hanceedWidget, [=](){
        QRect rc = geometry();
        rc.setHeight(contentHeight()+ArrowLineExpand_SPACING*2);
        setGeometry(rc);
    });
}

QList<DBaseExpand *> PropertyDialog::addExpandWidget(const QStringList &titleList)
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(m_scrollArea->widget()->layout());
    QList<DBaseExpand *> group;

    for (const QString &title : titleList) {
        DFMDArrowLineExpand *expand = new DFMDArrowLineExpand;//DArrowLineExpand;
        expand->setTitle(title);
        initExpand(layout, expand);
        group.push_back(expand);
    }

    return group;
}

void PropertyDialog::initTextShowFrame(const QString &text)
{
    m_textShowFrame = new QFrame(this);

    m_editButton = new DIconButton(m_textShowFrame);
    m_editButton->setObjectName("EditButton");
    m_editButton->setIcon(QIcon::fromTheme("edit-rename"));
    m_editButton->setIconSize({24, 24});
    m_editButton->setFixedSize(24, 24);
    m_editButton->setFlat(true);
    connect(m_editButton, &QPushButton::clicked, this, &PropertyDialog::renameFile);

    QString t = DFMGlobal::elideText(text, m_edit->size(), QTextOption::WrapAtWordBoundaryOrAnywhere, m_edit->font(), Qt::ElideMiddle, 0);
    QStringList labelTexts = t.split("\n");
    const int maxLineCount = 3;

    int textHeight = 0;
    QVBoxLayout *textShowLayout = new QVBoxLayout;

    for (int i = 0; i < labelTexts.length(); i++) {
        if (i > (maxLineCount - 1)) {
            break;
        }
        QString labelText = labelTexts.at(i);
        QLabel *label = new QLabel(labelText, m_textShowFrame);
        label->setAlignment(Qt::AlignHCenter);
        QHBoxLayout *hLayout = new QHBoxLayout;

        textHeight += label->fontInfo().pixelSize() + 10;

        hLayout->addStretch(1);
        hLayout->addWidget(label);
        if (i < (labelTexts.length() - 1) && i != (maxLineCount - 1)) {
            if (label->fontMetrics().width(labelText) > (m_edit->width() - 10)) {
                label->setFixedWidth(m_edit->width());
            }
        } else {
            // the final line of file name label, with a edit btn.
            if (labelTexts.length() >= maxLineCount) {
                for (int idx = i + 1; idx < labelTexts.length(); idx++) {
                    labelText += labelTexts.at(idx);
                }
            }

            if (label->fontMetrics().width(labelText) > (m_edit->width() - 2 * m_editButton->width()) && labelTexts.length() >= maxLineCount) {
                labelText = label->fontMetrics().elidedText(labelText, Qt::ElideMiddle, m_edit->width() - m_editButton->width());
            }
            label->setText(labelText);
            hLayout->addSpacing(2);
            hLayout->addWidget(m_editButton);
        }
        textShowLayout->addLayout(hLayout);
        hLayout->addStretch(1);
    }

    textShowLayout->setContentsMargins(0, 0, 0, 0);
    textShowLayout->setSpacing(0);
    m_textShowFrame->setLayout(textShowLayout);
    textShowLayout->addStretch(1);

    m_textShowFrame->setFixedHeight(textHeight + 15);

    if (m_editStackWidget->count() == 1) {
        m_editStackWidget->addWidget(m_textShowFrame);
    } else {
        m_editStackWidget->insertWidget(1, m_textShowFrame);
    }
    m_editStackWidget->setCurrentIndex(1);
    m_editStackWidget->setFixedHeight(m_textShowFrame->height());
}

QFrame *PropertyDialog::createBasicInfoWidget(const DAbstractFileInfoPointer &info)
{
    QFrame *widget = new QFrame(this);
    SectionKeyLabel *sizeSectionLabel = new SectionKeyLabel(QObject::tr("Size"));
    SectionKeyLabel *typeSectionLabel = new SectionKeyLabel(QObject::tr("Type"));
    SectionKeyLabel *TimeCreatedSectionLabel = new SectionKeyLabel(QObject::tr("Time created"));
    SectionKeyLabel *TimeReadSectionLabel = new SectionKeyLabel(QObject::tr("Time accessed"));
    SectionKeyLabel *TimeModifiedSectionLabel = new SectionKeyLabel(QObject::tr("Time modified"));
    SectionKeyLabel *sourcePathSectionLabel = new SectionKeyLabel(QObject::tr("Source path"));

    m_containSizeLabel = new SectionValueLabel(info->sizeDisplayName());
    m_folderSizeLabel = new SectionValueLabel;
    SectionValueLabel *typeLabel = new SectionValueLabel(info->mimeTypeDisplayName());
    SectionValueLabel *timeCreatedLabel = new SectionValueLabel(info->createdDisplayName());
    SectionValueLabel *timeReadLabel = new SectionValueLabel(info->lastReadDisplayName());
    SectionValueLabel *timeModifiedLabel = new SectionValueLabel(info->lastModifiedDisplayName());

    QFormLayout *layout = new QFormLayout;
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(16);
    layout->setLabelAlignment(Qt::AlignRight);

    if (info->isDir()) {
        SectionKeyLabel *fileAmountSectionLabel = new SectionKeyLabel(QObject::tr("Contains"));
        layout->addRow(sizeSectionLabel, m_folderSizeLabel);
        layout->addRow(fileAmountSectionLabel, m_containSizeLabel);
    } else {
        layout->addRow(sizeSectionLabel, m_containSizeLabel);
    }

    if (!info->isVirtualEntry()) {
        layout->addRow(typeSectionLabel, typeLabel);
    }

    if (info->isSymLink()) {
        SectionKeyLabel *linkPathSectionLabel = new SectionKeyLabel(QObject::tr("Location"));

        LinkSectionValueLabel *linkPathLabel = new LinkSectionValueLabel(info->symlinkTargetPath());
        linkPathLabel->setToolTip(info->symlinkTargetPath());
        linkPathLabel->setLinkTargetUrl(info->redirectedFileUrl());
        linkPathLabel->setOpenExternalLinks(true);
        linkPathLabel->setWordWrap(false);
        QString t = linkPathLabel->fontMetrics().elidedText(info->symlinkTargetPath(), Qt::ElideMiddle, 150);
        linkPathLabel->setText(t);
        layout->addRow(linkPathSectionLabel, linkPathLabel);
    }

    if (!info->isVirtualEntry()) {
        layout->addRow(TimeCreatedSectionLabel, timeCreatedLabel);
        layout->addRow(TimeReadSectionLabel, timeReadLabel);
        layout->addRow(TimeModifiedSectionLabel, timeModifiedLabel);
    }

    if (info->fileUrl().isTrashFile()) {
        QString pathStr = static_cast<const TrashFileInfo *>(info.constData())->sourceFilePath();
        SectionValueLabel *sourcePathLabel = new SectionValueLabel(pathStr);
        QString elidedStr = sourcePathLabel->fontMetrics().elidedText(pathStr, Qt::ElideMiddle, 150);
        sourcePathLabel->setToolTip(pathStr);
        sourcePathLabel->setWordWrap(false);
        sourcePathLabel->setText(elidedStr);
        layout->addRow(sourcePathSectionLabel, sourcePathLabel);
    }

    layout->setContentsMargins(15, 15, 30, 15);
    widget->setLayout(layout);
//    if (info->isSymLink()) {
//        widget->setFixedSize(width(), EXTEND_FRAME_MAXHEIGHT + 30);
//    }

    return widget;
}

ShareInfoFrame *PropertyDialog::createShareInfoFrame(const DAbstractFileInfoPointer &info)
{
    DAbstractFileInfoPointer infoPtr = info->canRedirectionFileUrl() ? DFileService::instance()->createFileInfo(nullptr, info->redirectedFileUrl())
                                                                     : info;
    ShareInfoFrame *frame = new ShareInfoFrame(infoPtr, this);
    //play animation after a folder is shared
    connect(frame, &ShareInfoFrame::folderShared, this, &PropertyDialog::flickFolderToSidebar);

    return frame;
}

QList<QPair<QString, QString> > PropertyDialog::createLocalDeviceInfoWidget(const DAbstractFileInfoPointer &info)
{
    QList<QPair<QString, QString> > results;

    if (!info) {
        qWarning("BUG: cannot create local device info because given device info pointer is not valid");
        return results;
    }

    QString fsType = info->extraProperties()["fsType"].toString();
    quint64 fsUsed = info->extraProperties()["fsUsed"].toULongLong();
    quint64 fsSize = info->extraProperties()["fsSize"].toULongLong();
    quint64 fileCount = 0;
    if (!info->redirectedFileUrl().isEmpty()) {
        fileCount = FileUtils::filesCount(info->redirectedFileUrl().toLocalFile());
    }

    static QHash<DFMRootFileInfo::ItemType, QString> devtypemap = {
        {DFMRootFileInfo::ItemType::UDisksRoot, QObject::tr("Local disk")},
        {DFMRootFileInfo::ItemType::UDisksData, QObject::tr("Local disk")},
        {DFMRootFileInfo::ItemType::UDisksFixed, QObject::tr("Local disk")},
        {DFMRootFileInfo::ItemType::UDisksRemovable, QObject::tr("Removable disk")},
        {DFMRootFileInfo::ItemType::UDisksOptical, QObject::tr("DVD")},
        {DFMRootFileInfo::ItemType::GvfsFTP, QObject::tr("Network shared directory")},
        {DFMRootFileInfo::ItemType::GvfsSMB, QObject::tr("Network shared directory")},
        {DFMRootFileInfo::ItemType::GvfsMTP, QObject::tr("Android mobile device")},
        {DFMRootFileInfo::ItemType::GvfsGPhoto2, QObject::tr("Apple mobile device")},
        {DFMRootFileInfo::ItemType::GvfsCamera, QObject::tr("Camera")}
    };
    results.append({QObject::tr("Device type"), devtypemap.value(static_cast<DFMRootFileInfo::ItemType>(info->fileType()), QObject::tr("Unknown device"))});
    results.append({QObject::tr("Total space"), FileUtils::formatSize(fsSize)});
    if (!fsType.isEmpty()) {
        results.append({QObject::tr("File system"), fsType});
    }
    results.append({QObject::tr("Contains"), (fileCount != 1 ? QObject::tr("%1 items") : QObject::tr("%1 item")).arg(fileCount)});
    results.append({QObject::tr("Free space"), FileUtils::formatSize(fsSize - fsUsed)});

    return results;
}

QFrame *PropertyDialog::createInfoFrame(const QList<QPair<QString, QString> >& properties)
{
    QFrame *widget = new QFrame(this);

    QFormLayout *layout = new QFormLayout;
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(16);
    layout->setLabelAlignment(Qt::AlignRight);

    for (const QPair<QString, QString> & kv : properties) {
        SectionKeyLabel *keyLabel = new SectionKeyLabel(kv.first, widget);
        SectionValueLabel *valLabel = new SectionValueLabel(kv.second, widget);
        layout->addRow(keyLabel, valLabel);
    }

    widget->setLayout(layout);
    widget->setFixedHeight(EXTEND_FRAME_MAXHEIGHT);
    return widget;
}

QListWidget *PropertyDialog::createOpenWithListWidget(const DAbstractFileInfoPointer &info)
{
    DUrl fileUrl = info->fileUrl();
    DAbstractFileInfoPointer infoPtr = info;
    while (infoPtr->canRedirectionFileUrl()) {
        if (fileUrl == infoPtr->redirectedFileUrl()) {
            break;
        }
        fileUrl = infoPtr->redirectedFileUrl();
        infoPtr = fileService->createFileInfo(nullptr, fileUrl);
    }
    QListWidget *listWidget = new QListWidget(this);
    listWidget->setSpacing(8);
    listWidget->setObjectName("OpenWithListWidget");
    m_OpenWithButtonGroup = new QButtonGroup(listWidget);
    listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QStringList recommendApps = mimeAppsManager->getRecommendedApps(fileUrl);

    QString mimeType = infoPtr->mimeType().name();
    QString defaultApp = mimeAppsManager->getDefaultAppDisplayNameByGio(mimeType);

    foreach (const QString &appFile, recommendApps) {
        if (!QFile::exists(appFile)) {
            continue;
        }
        DesktopFile df(appFile);

        QListWidgetItem *item = new QListWidgetItem;

        QCheckBox *itemBox = new QCheckBox(df.getDisplayName());
        itemBox->setObjectName("OpenWithItem");
        itemBox->setIcon(QIcon::fromTheme(df.getIcon()));
        itemBox->setIconSize(QSize(16, 16));
        itemBox->setProperty("appPath", appFile);
        itemBox->setProperty("mimeTypeName", mimeType);
        m_OpenWithButtonGroup->addButton(itemBox);
        item->setData(Qt::UserRole, df.getName());
        listWidget->addItem(item);
        listWidget->setItemWidget(item, itemBox);

        if (df.getLocalName() == defaultApp) {
            itemBox->setChecked(true);
        }

    }

    int listHeight = 2;
    for (int i = 0; i < listWidget->count(); i++) {
        QListWidgetItem *item = listWidget->item(i);
        item->setFlags(Qt::NoItemFlags);
        int h = listWidget->itemWidget(item)->height();
        item->setSizeHint(QSize(item->sizeHint().width(), h));
        listHeight += h;
    }

    listWidget->setFixedHeight(EXTEND_FRAME_MAXHEIGHT);
    listWidget->setFixedWidth(300);

    connect(m_OpenWithButtonGroup, SIGNAL(buttonClicked(QAbstractButton *)),
            this, SLOT(onOpenWithBntsChecked(QAbstractButton *)));

    return listWidget;
}

QFrame *PropertyDialog::createAuthorityManagementWidget(const DAbstractFileInfoPointer &info)
{
    QFrame *widget = new QFrame;
    QFormLayout *layout = new QFormLayout;

    QComboBox *ownerBox = new QComboBox;
    QComboBox *groupBox = new QComboBox;
    QComboBox *otherBox = new QComboBox;

    // these are for file or folder, folder will with executable index.
    int readWriteIndex = 0, readOnlyIndex = 0;

    QStringList authorityList;

    authorityList << QObject::tr("Access denied") // 0
                  << QObject::tr("Executable") // 1
                  << QObject::tr("Write only") // 2
                  << QObject::tr("Write only") // 3 with x
                  << QObject::tr("Read only")  // 4
                  << QObject::tr("Read only")  // 5 with x
                  << QObject::tr("Read-write") // 6
                  << QObject::tr("Read-write"); // 7 with x

    if (info->isFile()) {
        // append `Executable` string
        QString append = QStringLiteral(" , ") + QObject::tr("Executable");
        authorityList[3] += append;
        authorityList[5] += append;
        authorityList[7] += append;

        // file: read is read, read-write is read-write
        readOnlyIndex = 4;
        readWriteIndex = 6;
    }

    if (info->isDir()) {
        // folder: read is read and executable, read-write is read-write and executable
        readOnlyIndex = 5;
        readWriteIndex = 7;
    }

    // enumFlag should be 0~7, this is just a check to avoid runtime error
    auto getPermissionString = [ &authorityList ](int enumFlag) {
        enumFlag &= 0x0007;
        if (enumFlag < 0 || enumFlag > 7) {
            return QStringLiteral("..what?");
        } else {
            return authorityList[enumFlag];
        }
    };

    // set QComboBox, notice this permission number is not just 0~7
    auto setComboBoxByPermission = [ = ](QComboBox * cb, int permission, int offset) {
        int index = permission >> offset;
        if (index == readWriteIndex) {
            cb->setCurrentIndex(0);
        } else if (index == readOnlyIndex) {
            cb->setCurrentIndex(1);
        } else {
            cb->addItem(getPermissionString(index), QVariant(permission));
            cb->setCurrentIndex(2);
        }
    };

    // when change the index...
    auto onComboBoxChanged = [ = ]() {
        DFileService::instance()->setPermissions(this, m_url,
                                                 QFileDevice::Permissions(ownerBox->currentData().toInt()) |
                                                 /*(info->permissions() & 0x0700) |*/
                                                 QFileDevice::Permissions(groupBox->currentData().toInt()) |
                                                 QFileDevice::Permissions((otherBox->currentData().toInt())));
    };

    if (info->isDir()) {
        ownerBox->addItem(authorityList[readWriteIndex], QVariant(QFile::WriteOwner | QFile::ReadOwner | QFile::ExeOwner));
        ownerBox->addItem(authorityList[readOnlyIndex], QVariant(QFile::ReadOwner | QFile::ExeOwner));
        groupBox->addItem(authorityList[readWriteIndex], QVariant(QFile::WriteGroup | QFile::ReadGroup | QFile::ExeGroup));
        groupBox->addItem(authorityList[readOnlyIndex], QVariant(QFile::ReadGroup | QFile::ExeGroup));
        otherBox->addItem(authorityList[readWriteIndex], QVariant(QFile::WriteOther | QFile::ReadOther | QFile::ExeOther));
        otherBox->addItem(authorityList[readOnlyIndex], QVariant(QFile::ReadOther | QFile::ExeOther));
    } else {
        ownerBox->addItem(authorityList[readWriteIndex], QVariant(QFile::WriteOwner | QFile::ReadOwner));
        ownerBox->addItem(authorityList[readOnlyIndex], QVariant(QFile::ReadOwner));
        groupBox->addItem(authorityList[readWriteIndex], QVariant(QFile::WriteGroup | QFile::ReadGroup));
        groupBox->addItem(authorityList[readOnlyIndex], QVariant(QFile::ReadGroup));
        otherBox->addItem(authorityList[readWriteIndex], QVariant(QFile::WriteOther | QFile::ReadOther));
        otherBox->addItem(authorityList[readOnlyIndex], QVariant(QFile::ReadOther));
    }


    setComboBoxByPermission(ownerBox, info->permissions() & 0x7000, 12);
    setComboBoxByPermission(groupBox, info->permissions() & 0x0070, 4);
    setComboBoxByPermission(otherBox, info->permissions() & 0x0007, 0);

    layout->setLabelAlignment(Qt::AlignRight);
    layout->addRow(QObject::tr("Owner"), ownerBox);
    layout->addRow(QObject::tr("Group"), groupBox);
    layout->addRow(QObject::tr("Others"), otherBox);

    if (info->isFile()) {
        m_executableCheckBox = new QCheckBox;
        m_executableCheckBox->setText(tr("Allow to execute as program"));
        connect(m_executableCheckBox, &QCheckBox::toggled, this, &PropertyDialog::toggleFileExecutable);
        if (info->ownerId() != getuid()) {
            m_executableCheckBox->setDisabled(true);
        }
        if (info->permission(QFile::ExeUser) || info->permission(QFile::ExeGroup) || info->permission(QFile::ExeOther)) {
            m_executableCheckBox->setChecked(true);
        }
        layout->addRow(m_executableCheckBox);
    }


    layout->setContentsMargins(45, 10, 15, 10);
    widget->setLayout(layout);

    connect(ownerBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), widget, onComboBoxChanged);
    connect(groupBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), widget, onComboBoxChanged);
    connect(otherBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), widget, onComboBoxChanged);

    if (info->ownerId() != getuid()) {
        ownerBox->setDisabled(true);
        groupBox->setDisabled(true);
        otherBox->setDisabled(true);
    }

    return widget;
}
