/* Copyright 2026 PollyMC-Continued Contributors
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

#include "CommandPalette.h"

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPalette>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

CommandEntry CommandEntry::fromAction(QAction* action)
{
    CommandEntry entry;
    entry.text = QString(action->text()).remove('&');
    if (entry.text.isEmpty())
        entry.text = action->objectName();
    entry.shortcut = action->shortcut();
    entry.sourceAction = action;
    entry.isEnabled = [action] { return action->isEnabled(); };
    entry.trigger = [action] { action->trigger(); };
    return entry;
}

// Score: higher wins. Exact prefix > word boundary > substring > scattered.
static int fuzzyScore(const QString& needle, const QString& haystack, bool& matched)
{
    matched = false;
    if (needle.isEmpty())
        return 0;
    const QString n = needle.toLower();
    const QString h = haystack.toLower();
    if (h.startsWith(n)) {
        matched = true;
        return 4000 - h.length();
    }
    // word boundary: match right after a space, -, _ or /
    int pos = 0;
    while ((pos = h.indexOf(n, pos)) != -1) {
        const QChar prev = pos > 0 ? h[pos - 1] : QChar(' ');
        if (prev == ' ' || prev == '-' || prev == '_' || prev == '/') {
            matched = true;
            return 3000 - pos;
        }
        pos += n.length();
    }
    if (h.contains(n)) {
        matched = true;
        return 2000 - h.indexOf(n);
    }
    // scattered: chars in order, penalize gaps
    int hi = 0, gaps = 0;
    for (const QChar c : n) {
        const int found = h.indexOf(c, hi);
        if (found == -1)
            return -1;
        gaps += found - hi;
        hi = found + 1;
    }
    matched = true;
    return 1000 - qMin(gaps, 999);
}

// Icon + text left, shortcut in a fixed right column, substring bolded.
class ShortcutDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setQuery(const QString& query) { m_query = query; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        if (option.state & QStyle::State_Selected)
            painter->fillRect(option.rect, option.palette.highlight());
        const bool enabled = index.data(Qt::UserRole + 2).toBool();
        const bool selected = option.state & QStyle::State_Selected;
        const QColor textColor = selected ? option.palette.highlightedText().color()
                                          : enabled ? option.palette.text().color()
                                                    : option.palette.color(QPalette::Disabled, QPalette::Text);
        const int pad = 6;
        const int iconSlot = 22;
        const int shortcutCol = 120;
        const QRect content = option.rect.adjusted(pad, 0, -pad, 0);
        // fixed icon slot keeps text aligned whether or not an icon exists
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull())
            icon.paint(painter, QRect(content.left(), content.center().y() - 8, 16, 16));
        // fixed right column keeps shortcuts aligned across row widths
        const QString shortcut = index.data(Qt::UserRole + 1).toString();
        if (!shortcut.isEmpty()) {
            painter->setPen(selected ? option.palette.highlightedText().color()
                                     : option.palette.color(QPalette::Disabled, QPalette::Text));
            painter->drawText(QRect(content.right() - shortcutCol, content.top(), shortcutCol, content.height()),
                              Qt::AlignRight | Qt::AlignVCenter, shortcut);
        }
        QRect textRect(content.left() + iconSlot, content.top(), content.width() - iconSlot - shortcutCol - pad,
                       content.height());
        textRect = QStyle::visualRect(option.direction, content, textRect);
        const QFontMetrics fm(option.font);
        const QString text = fm.elidedText(index.data().toString(), Qt::ElideRight, textRect.width());
        int matchAt = -1;
        if (!m_query.isEmpty())
            matchAt = text.indexOf(m_query, 0, Qt::CaseInsensitive);
        painter->setPen(textColor);
        if (matchAt < 0) {
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        } else {
            const int y = textRect.top() + fm.ascent() + (textRect.height() - fm.height()) / 2;
            int x = textRect.left();
            painter->drawText(x, y, text.left(matchAt));
            x += fm.horizontalAdvance(text.left(matchAt));
            QFont bold(option.font);
            bold.setBold(true);
            painter->setFont(bold);
            if (!selected)
                painter->setPen(option.palette.highlight().color());
            painter->drawText(x, y, text.mid(matchAt, m_query.length()));
            x += QFontMetrics(bold).horizontalAdvance(text.mid(matchAt, m_query.length()));
            painter->setFont(option.font);
            painter->setPen(textColor);
            painter->drawText(x, y, text.mid(matchAt + m_query.length()));
        }
        painter->restore();
    }

   private:
    QString m_query;
};

class CommandPalette::Model : public QAbstractListModel {
   public:
    explicit Model(QList<CommandEntry> entries, QObject* parent = nullptr) : QAbstractListModel(parent), m_entries(std::move(entries)) {}
    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : m_entries.size(); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
            return {};
        const auto& e = m_entries.at(index.row());
        if (role == Qt::DisplayRole)
            return e.text;
        if (role == Qt::DecorationRole)
            return e.sourceAction ? e.sourceAction->icon() : QIcon();
        if (role == Qt::UserRole + 1)
            return e.shortcut.toString();
        // enabled state is queried lazily on every paint, never cached
        if (role == Qt::UserRole + 2)
            return e.isEnabled ? e.isEnabled() : true;
        return {};
    }
    const CommandEntry& entryAt(int row) const { return m_entries.at(row); }

   private:
    QList<CommandEntry> m_entries;
};

class CommandPalette::FuzzyFilter : public QSortFilterProxyModel {
   public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setQuery(const QString& query) { m_query = query; }
    const QString& query() const { return m_query; }

   protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        Q_UNUSED(parent);
        const CommandEntry& e = static_cast<const Model*>(sourceModel())->entryAt(row);
        if (e.hideByDefault) {
            // hidden unless the query matches at word boundary or better
            if (m_query.isEmpty())
                return false;
            bool matched = false;
            const int score = fuzzyScore(m_query, e.text, matched);
            return matched && score >= 2000;
        }
        if (m_query.isEmpty())
            return true;
        bool matched = false;
        fuzzyScore(m_query, e.text, matched);
        return matched;
    }
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        const auto* model = static_cast<const Model*>(sourceModel());
        if (m_query.isEmpty()) {
            // curated shortlist: prioritized actions first, rest in order
            const int pl = model->entryAt(left.row()).priority;
            const int pr = model->entryAt(right.row()).priority;
            if (pl != pr)
                return pl > pr;
            return left.row() < right.row();
        }
        bool ml = false, mr = false;
        const int sl = fuzzyScore(m_query, sourceModel()->data(left).toString(), ml) + model->entryAt(left.row()).weight;
        const int sr = fuzzyScore(m_query, sourceModel()->data(right).toString(), mr) + model->entryAt(right.row()).weight;
        if (sl != sr)
            return sl > sr;  // best score first
        const int pl = model->entryAt(left.row()).priority;
        const int pr = model->entryAt(right.row()).priority;
        if (pl != pr)
            return pl > pr;
        return left.row() < right.row();
    }

   private:
    QString m_query;
};

CommandPalette::CommandPalette(QList<CommandEntry> entries, QWidget* parent) : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setMinimumSize(560, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 8);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Type a command..."));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(32);
    QFont searchFont = m_search->font();
    searchFont.setPointSize(searchFont.pointSize() + 2);
    m_search->setFont(searchFont);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_count = new QLabel(this);
    m_count->setAlignment(Qt::AlignRight);
    m_count->setVisible(false);
    layout->addWidget(m_count);

    m_model = new Model(std::move(entries), this);
    m_filter = new FuzzyFilter(this);
    m_filter->setSourceModel(m_model);

    m_delegate = new ShortcutDelegate(this);
    m_list = new QListView(this);
    m_list->setModel(m_filter);
    m_list->setItemDelegate(m_delegate);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);
    m_filter->invalidateRowsFilter();
    m_filter->sort(0);  // curated shortlist before the first keystroke

    auto* hint = new QLabel(tr("↑↓ navigate · PgUp/PgDn jump · Enter run · Esc close"), this);
    hint->setEnabled(false);
    layout->addWidget(hint);
    // TODO: add a categories line here once group headers land

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::onQueryChanged);
    connect(m_list, &QListView::activated, this, &CommandPalette::runSelected);
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::showEvent(QShowEvent* event)
{
    placeOverParent();
    QDialog::showEvent(event);
    m_search->setFocus();
    selectFirstRow();
}

void CommandPalette::placeOverParent()
{
    if (const QWidget* p = parentWidget()) {
        const int w = qMin(720, qMax(560, p->width() * 3 / 5));
        resize(w, 460);
        move(p->mapToGlobal(QPoint((p->width() - w) / 2, int(p->height() * 0.15))));
    }
}

bool CommandPalette::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (obj == m_search || obj == m_list) {
            const int row = m_list->currentIndex().row();
            if (key->key() == Qt::Key_Down) {
                m_list->setCurrentIndex(m_filter->index(qMin(row + 1, m_filter->rowCount() - 1), 0));
                return true;
            }
            if (key->key() == Qt::Key_Up) {
                m_list->setCurrentIndex(m_filter->index(qMax(row - 1, 0), 0));
                return true;
            }
            if (key->key() == Qt::Key_Home) {
                m_list->setCurrentIndex(m_filter->index(0, 0));
                return true;
            }
            if (key->key() == Qt::Key_End) {
                if (m_filter->rowCount() > 0)
                    m_list->setCurrentIndex(m_filter->index(m_filter->rowCount() - 1, 0));
                return true;
            }
            if (key->key() == Qt::Key_PageDown) {
                const int next = qMin(m_list->currentIndex().row() + 10, m_filter->rowCount() - 1);
                if (m_filter->rowCount() > 0)
                    m_list->setCurrentIndex(m_filter->index(next, 0));
                return true;
            }
            if (key->key() == Qt::Key_PageUp) {
                const int prev = qMax(m_list->currentIndex().row() - 10, 0);
                if (m_filter->rowCount() > 0)
                    m_list->setCurrentIndex(m_filter->index(prev, 0));
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                runSelected();
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

void CommandPalette::onQueryChanged(const QString& query)
{
    m_filter->setQuery(query);
    m_delegate->setQuery(query);
    m_filter->sort(0);
    m_count->setText(tr("%1 of %2").arg(m_filter->rowCount()).arg(m_model->rowCount()));
    m_count->setVisible(!query.isEmpty());
    m_list->viewport()->update();
    selectFirstRow();
}

void CommandPalette::selectFirstRow()
{
    if (m_filter->rowCount() > 0)
        m_list->setCurrentIndex(m_filter->index(0, 0));
}

void CommandPalette::runSelected()
{
    const QModelIndex proxyIndex = m_list->currentIndex();
    if (!proxyIndex.isValid())
        return;
    const CommandEntry entry = m_model->entryAt(m_filter->mapToSource(proxyIndex).row());
    if (entry.isEnabled && !entry.isEnabled())
        return;
    // close first so the action runs without the modal dialog in the way
    if (entry.closeAfterTrigger)
        accept();
    if (entry.trigger)
        QTimer::singleShot(0, entry.trigger);
}
