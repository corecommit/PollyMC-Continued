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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPalette>
#include <QShowEvent>
#include <QSortFilterProxyModel>
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

// Right-aligns the shortcut, grays disabled rows.
class ShortcutDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString shortcut = index.data(Qt::UserRole + 1).toString();
        if (!shortcut.isEmpty())
            opt.text = opt.text + "   " + shortcut;
        if (!index.data(Qt::UserRole + 2).toBool())
            opt.palette.setColor(QPalette::Text, opt.palette.color(QPalette::Disabled, QPalette::Text));
        QStyledItemDelegate::paint(painter, opt, index);
    }
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
        if (role == Qt::UserRole)
            return QVariant::fromValue(index.row());
        if (role == Qt::UserRole + 1)
            return e.shortcut.toString();
        // enabled state is queried lazily on every paint, never cached
        if (role == Qt::UserRole + 2)
            return e.isEnabled ? e.isEnabled() : true;
        if (role == Qt::ForegroundRole && e.isEnabled && !e.isEnabled())
            return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
        return {};
    }
    CommandEntry entryAt(int row) const { return m_entries.value(row); }

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
        if (m_query.isEmpty())
            return true;
        bool matched = false;
        fuzzyScore(m_query, sourceModel()->index(row, 0, parent).data().toString(), matched);
        return matched;
    }
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        if (m_query.isEmpty())
            return false;
        const auto* model = static_cast<const Model*>(sourceModel());
        bool ml = false, mr = false;
        const int sl = fuzzyScore(m_query, sourceModel()->data(left).toString(), ml) + model->entryAt(left.row()).weight;
        const int sr = fuzzyScore(m_query, sourceModel()->data(right).toString(), mr) + model->entryAt(right.row()).weight;
        if (sl != sr)
            return sl > sr;  // best score first
        return left.row() < right.row();  // stable original order
    }

   private:
    QString m_query;
};

CommandPalette::CommandPalette(QList<CommandEntry> entries, QWidget* parent) : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setMinimumSize(480, 400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 8);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Type a command..."));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_model = new Model(std::move(entries), this);
    m_filter = new FuzzyFilter(this);
    m_filter->setSourceModel(m_model);

    m_list = new QListView(this);
    m_list->setModel(m_filter);
    m_list->setItemDelegate(new ShortcutDelegate(this));
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);

    auto* hint = new QLabel(tr("↑↓ to navigate · Enter to run · Esc to close"), this);
    hint->setEnabled(false);
    layout->addWidget(hint);

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::onQueryChanged);
    connect(m_list, &QListView::activated, this, &CommandPalette::runSelected);
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (const QWidget* p = parentWidget()) {
        const int w = qMax(minimumWidth(), p->width() * 3 / 5);
        resize(w, 400);
        move(p->mapToGlobal(QPoint((p->width() - w) / 2, qMax(0, (p->height() - 400) / 3))));
    }
    m_search->setFocus();
    selectFirstRow();
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
    m_filter->invalidate();
    if (query.isEmpty())
        m_filter->sort(-1);  // back to source order
    else
        m_filter->sort(0);
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
